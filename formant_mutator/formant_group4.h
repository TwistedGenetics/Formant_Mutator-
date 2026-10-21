/*
 *  File: formant_group4.h
 *
 *  FORMANT MUTATOR - 4-voice SIMD renderer (used by the microKORG 2 unit).
 *
 *  The microKORG 2 runs oscillators polyphonically on a Cortex-A7. Korg's own
 *  examples (microkorg2/vox) process voices four at a time in NEON lanes.
 *  This renderer does the same for FORMANT MUTATOR: lane j = voice j of a
 *  group of four. Control-rate work (smoothing, LFO, envelope, vowel
 *  interpolation, filter coefficients) still runs per voice in
 *  Engine::controlUpdate(); only the per-sample loop is vectorised.
 *
 *  Differences from the scalar Engine::renderBlock (NTS-1 mkII):
 *    - oscillators are branch-free per lane (polyBLEP via selects); the
 *      waveform morph is a weighted sum of sine/triangle/saw/offset-saw
 *    - sine comes from a refined parabolic approximation (~0.1 % error)
 *      instead of the lookup table (table gathers do not vectorise)
 *    - noise uses a per-lane xorshift
 *  Everything else (formant SVFs, feedback, shapers, limiter) is the same
 *  maths. The test harness checks this renderer against the scalar one.
 *
 *  Written with GCC vector extensions: compiles to NEON on ARM and to SSE
 *  on the desktop test build.
 */

#ifndef FORMANT_GROUP4_H_
#define FORMANT_GROUP4_H_

#include <stdint.h>
#include <string.h>
#include "formant_engine.h"

namespace fmut {

typedef float    g4f __attribute__((vector_size(16)));
typedef int      g4i __attribute__((vector_size(16)));   // = type of a float-vector comparison
typedef uint32_t g4u __attribute__((vector_size(16)));

static FM_INLINE g4f g4splat(float v) { return (g4f){v, v, v, v}; }
static FM_INLINE g4f g4sel(g4i m, g4f a, g4f b) { return m ? a : b; }
static FM_INLINE g4f g4abs(g4f x) {             // clear the sign bits
  g4u b;
  memcpy(&b, &x, 16);
  b &= 0x7FFFFFFFu;
  memcpy(&x, &b, 16);
  return x;
}
static FM_INLINE g4f g4min(g4f a, g4f b) { return g4sel(a < b, a, b); }
static FM_INLINE g4f g4max(g4f a, g4f b) { return g4sel(a > b, a, b); }
static FM_INLINE g4f g4clamp(g4f x, float lo, float hi) { return g4min(g4max(x, g4splat(lo)), g4splat(hi)); }

// Cubic soft clip, branch-free (clamp to +-1.5, then x - 4x^3/27).
static FM_INLINE g4f g4softclip(g4f x) {
  x = g4clamp(x, -1.5f, 1.5f);
  return x - 0.148148148f * x * x * x;
}
// sin(2 pi t) for t in [0, 1): parabolic approximation + one refinement.
static FM_INLINE g4f g4sin2pi(g4f t) {
  const g4f y  = 2.f * t - 1.f;                  // [-1, 1)
  const g4f s0 = 4.f * y - 4.f * y * g4abs(y);   // ~ sin(pi y)
  const g4f s  = 0.225f * (s0 * g4abs(s0) - s0) + s0;
  return -s;                                     // sin(2 pi t) = -sin(pi y)
}
static FM_INLINE g4f g4wrap1(g4f t) {            // single wrap into [0, 1)
  t = g4sel(t >= 1.f, t - 1.f, t);
  return g4sel(t < 0.f, t + 1.f, t);
}
static FM_INLINE g4f g4tri(g4f t) {              // 0 at t=0, +1 at 0.25
  const g4f x = g4wrap1(t + 0.25f);
  return 1.f - 4.f * g4abs(x - 0.5f);
}
static FM_INLINE g4f g4saw(g4f t, g4f dt, g4f inv) {   // polyBLEP saw
  const g4f u1 = t * inv;
  const g4f r1 = u1 + u1 - u1 * u1 - 1.f;
  const g4f u2 = (t - 1.f) * inv;
  const g4f r2 = u2 * u2 + u2 + u2 + 1.f;
  const g4f blep = g4sel(t < dt, r1, g4sel(t > 1.f - dt, r2, g4splat(0.f)));
  return 2.f * t - 1.f - blep;
}
static FM_INLINE g4f g4recip(g4f d) {            // recip_fast, 4 lanes
  g4i bits;
  memcpy(&bits, &d, 16);
  bits = 0x7EF311C7 - bits;
  g4f r;
  memcpy(&r, &bits, 16);
  r = r * (2.f - d * r);
  r = r * (2.f - d * r);
  return r;
}
static FM_INLINE g4f g4frac(g4f x) {             // x - floor(x), per lane
  g4f r;
  for (int j = 0; j < 4; ++j) {
    int32_t i = (int32_t)x[j];
    if (x[j] < (float)i) --i;
    r[j] = x[j] - (float)i;
  }
  return r;
}

struct Group4 {
  // Render n (<= kCtrlBlock) samples for four engines. All four pointers must
  // be valid (use spare engines for unused lanes). out[i] holds the four
  // voices' samples for frame i.
  static void render(Engine *const e[4], g4f *out, uint32_t n) {
    for (int j = 0; j < 4; ++j) e[j]->controlUpdate(n);
    const int mode = e[0]->mode_;
    const ModeDef &md = kModes[mode];

    // ---- gather per-lane block constants ----
    g4f w1, w2, wS, inv1, inv2, aSin, aTri, aS1, aS2, off, norm, pm, ring, pre,
        noise, fb, fm, bank, subPre, subPost, drvIn, bias, foldMix, outScale;
    g4f ph1, ph2, phS, o2p, fbLp, fbPrev, dcX1, dcY1, postLp;
    g4f ic1[kNumForm], ic2[kNumForm], cG[kNumForm], cK[kNumForm], cGn[kNumForm], iG[kNumForm];
    g4u rng;
    for (int j = 0; j < 4; ++j) {
      const Engine &v = *e[j];
      w1[j] = v.w1_; w2[j] = v.w2_; wS[j] = v.wSub_;
      inv1[j] = 1.f / (v.w1_ > 1e-6f ? v.w1_ : 1e-6f);
      inv2[j] = 1.f / (v.w2_ > 1e-6f ? v.w2_ : 1e-6f);
      // waveform morph as weights (same regions as Engine::shapeOsc)
      float s = 0.f, t = 0.f, s1 = 0.f, s2 = 0.f, o = 0.5f;
      const float f = v.bFrac_;
      switch (v.bReg_) {
        case 0:  s = 1.f - f; t = f; break;                  // sine -> tri
        case 1:  t = 1.f - f; s1 = f; break;                 // tri -> saw
        case 2:  s1 = 1.f - 2.f * f; s2 = f; o = 0.5f; break;// saw -> square
        default: s1 = -1.f; s2 = 1.f; o = -v.pw_; break;     // pulse
      }
      aSin[j] = s; aTri[j] = t; aS1[j] = s1; aS2[j] = s2; off[j] = o;
      norm[j] = v.srcNorm_; pm[j] = v.pmAmt_; ring[j] = v.ringAmt_; pre[j] = v.preDrive_;
      noise[j] = v.noiseAmt_; fb[j] = v.fbAmt_; fm[j] = v.fmAmt_;
      bank[j] = md.makeup * v.pitchComp_;
      subPre[j] = md.subPre * v.subScale_; subPost[j] = md.subPost * v.subScale_;
      drvIn[j] = v.driveGain_ * kShaperIn;
      bias[j] = 0.3f * v.sDrive_; foldMix[j] = v.sDrive_;
      outScale[j] = v.outComp_ * (1.f + 0.3f * v.sDrive_) * (1.f / kShaperIn) * kOutGain;
      ph1[j] = v.ph1_; ph2[j] = v.ph2_; phS[j] = v.phSub_; o2p[j] = v.o2Prev_;
      fbLp[j] = v.fbLp_; fbPrev[j] = v.fbPrev_; dcX1[j] = v.dcX1_; dcY1[j] = v.dcY1_; postLp[j] = v.postLp_;
      rng[j] = v.rng_ ? v.rng_ : 0x12345678u;
      for (int k = 0; k < kNumForm; ++k) {
        ic1[k][j] = v.svf_[k].ic1; ic2[k][j] = v.svf_[k].ic2;
        // Frequency (G) ramps per sample as in the scalar engine; damping (K)
        // and gain (Gn) step once per 32-sample block to keep the SIMD
        // working set inside the 16 NEON registers.
        cG[k][j] = v.curG_[k]; iG[k][j] = v.incG_[k];
        cK[k][j] = v.curK_[k] + v.incK_[k] * (float)n;
        cGn[k][j] = v.curGn_[k] + v.incGn_[k] * (float)n;
      }
    }
    // Which waveform parts are needed this block (skip the rest).
    bool useSin = false, useTri = false, useS2 = false, useS1 = false;
    for (int j = 0; j < 4; ++j) {
      useSin |= aSin[j] != 0.f; useTri |= aTri[j] != 0.f;
      useS1 |= aS1[j] != 0.f;   useS2 |= aS2[j] != 0.f;
    }
    const bool usePm = (mode == kMetal);
    const bool useNoise = (mode == kGhost);
    const bool useSubPre = (md.subPre > 0.f);
    const bool useSubPost = (md.subPost > 0.f);
    const g4f zero = g4splat(0.f);

    for (uint32_t s = 0; s < n; ++s) {
      // --- oscillators ---
      g4f p1 = ph1;
      if (usePm) p1 = g4wrap1(p1 + pm * o2p);
      g4f o1 = zero, o2 = zero;
      if (useSin) { o1 += aSin * g4sin2pi(p1); o2 += aSin * g4sin2pi(ph2); }
      if (useTri) { o1 += aTri * g4tri(p1);    o2 += aTri * g4tri(ph2); }
      if (useS1)  { o1 += aS1 * g4saw(p1, w1, inv1); o2 += aS1 * g4saw(ph2, w2, inv2); }
      if (useS2)  {
        o1 += aS2 * g4saw(g4wrap1(p1 + off), w1, inv1);
        o2 += aS2 * g4saw(g4wrap1(ph2 + off), w2, inv2);
      }
      o2p = o2;
      const g4f subSin = g4sin2pi(phS);

      g4f src;
      if (mode == kMetal) {
        const g4f sum = 0.5f * (o1 + o2);
        src = sum + ring * (1.3f * o1 * o2 - sum);
      } else {
        src = 0.5f * (o1 + o2);
      }
      src *= norm;

      if (mode == kGrowl) {
        src += subPre * g4softclip(2.5f * subSin);
        src = g4softclip(src * pre);
      } else if (useSubPre) {
        src += subPre * subSin;
      }
      if (useNoise) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const g4u bits = (rng >> 9) | 0x3F800000u;       // float in [1, 2)
        g4f u;
        memcpy(&u, &bits, 16);
        src += noise * (2.f * u - 3.f);
      }
      if (mode == kGrowl) {
        fbLp += 0.3f * (fbPrev - fbLp);
        src += fb * g4softclip(1.5f * fbLp);
      }

      // --- formant bank (lanes = voices) ---
      const g4f modSig = (mode == kGrowl) ? subSin : ((mode == kTalk) ? g4sin2pi(ph1) : o1);
      const g4f fmv = g4clamp(1.f + fm * modSig, 0.3f, 2.2f);
      g4f y = zero;
      for (int k = 0; k < kNumForm; ++k) {
        cG[k] += iG[k];
        const g4f g = g4clamp(cG[k] * fmv, 0.002f, 3.f);
        const g4f r = g4recip(1.f + g * (g + cK[k]));
        const g4f v3 = src - ic2[k];
        const g4f v1 = r * ic1[k] + (g * r) * v3;
        const g4f v2 = ic2[k] + g * v1;
        ic1[k] = 2.f * v1 - ic1[k];
        ic2[k] = 2.f * v2 - ic2[k];
        y += cGn[k] * v1;
      }
      fbPrev = y;
      y *= bank;
      if (useSubPost) y += subPost * subSin;

      // --- nonlinear stage ---
      const g4f x = y * drvIn;
      g4f sh;
      if (mode == kGrowl) {
        sh = g4softclip(x + bias) - g4softclip(bias);
      } else if (mode == kMetal) {
        const g4f c = g4softclip(x);
        const g4f fold = g4sin2pi(g4frac(x * 0.25f));
        sh = c + foldMix * (fold - c);
      } else {
        sh = g4softclip(x);
      }
      sh *= outScale;

      const g4f dc = sh - dcX1 + 0.9985f * dcY1;
      dcX1 = sh; dcY1 = dc;
      postLp += 0.88f * (dc - postLp);

      // safety limiter (transparent below 0.6)
      const g4f ao = g4abs(postLp);
      const g4f lim = 0.6f + 0.38f * g4softclip((ao - 0.6f) * 2.5f);
      const g4f sgn = g4sel(postLp < 0.f, g4splat(-1.f), g4splat(1.f));
      out[s] = g4sel(ao < 0.6f, postLp, sgn * lim);

      // --- phase advance ---
      ph1 += w1; ph1 = g4sel(ph1 >= 1.f, ph1 - 1.f, ph1);
      ph2 += w2; ph2 = g4sel(ph2 >= 1.f, ph2 - 1.f, ph2);
      phS += wS; phS = g4sel(phS >= 1.f, phS - 1.f, phS);
    }

    // ---- scatter state back ----
    for (int j = 0; j < 4; ++j) {
      Engine &v = *e[j];
      v.ph1_ = ph1[j]; v.ph2_ = ph2[j]; v.phSub_ = phS[j]; v.o2Prev_ = o2p[j];
      v.fbLp_ = fbLp[j]; v.fbPrev_ = fbPrev[j]; v.dcX1_ = dcX1[j]; v.dcY1_ = dcY1[j]; v.postLp_ = postLp[j];
      v.rng_ = rng[j];
      for (int k = 0; k < kNumForm; ++k) {
        v.svf_[k].ic1 = ic1[k][j]; v.svf_[k].ic2 = ic2[k][j];
        v.curG_[k] = cG[k][j]; v.curK_[k] = cK[k][j]; v.curGn_[k] = cGn[k][j];
      }
    }
  }
};

}  // namespace fmut

#endif  // FORMANT_GROUP4_H_
