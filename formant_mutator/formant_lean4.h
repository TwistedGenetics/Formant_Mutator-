/*
 *  File: formant_lean4.h
 *
 *  FORMANT MUTATOR - lean 4-voice renderer for the microKORG 2.
 *
 *  The full engine (and its SIMD port, formant_group4.h) proved too heavy
 *  for the microKORG 2's oscillator budget: a diagnostic unit with the same
 *  voice/output handling played cleanly while FMutator crackled. This
 *  renderer follows the cost profile of Korg's own microkorg2/vox:
 *
 *    - filter coefficients are computed once per render call (per audio
 *      buffer), not per sample - exactly what vox does
 *    - three formants (F1-F3) instead of four
 *    - no audio-rate formant FM (needed a reciprocal per filter per sample);
 *      in TALK the GROWL knob drives the source into a soft clipper instead
 *    - per-voice control work runs only for sounding voices
 *    - the output limiter is a single soft clip
 *    - saws are DPW (differentiated parabolic waveform, as in vox): branch-
 *      free, ~3 operations each, instead of polyBLEP
 *
 *  Same oscillators, modes, vowel tables, envelope, LFO, sub, feedback and
 *  shapers as the full engine otherwise. Lanes = voices (NEON on ARM).
 */

#ifndef FORMANT_LEAN4_H_
#define FORMANT_LEAN4_H_

#include "formant_group4.h"   // g4f vector helpers

namespace fmut {

static const int kLeanForm = 3;

struct Lean4 {
  // Render n samples for four engines; live[j] = false lanes are skipped for
  // control work and output silence. All four pointers must be valid.
  static void render(Engine *const e[4], const bool live[4], g4f *out, uint32_t n) {
    int first = -1;
    for (int j = 0; j < 4; ++j)
      if (live[j]) { e[j]->controlUpdate(n); if (first < 0) first = j; }
    if (first < 0) { for (uint32_t s = 0; s < n; ++s) out[s] = g4splat(0.f); return; }
    const int mode = e[first]->mode_;
    const ModeDef &md = kModes[mode];

    g4f w1, w2, wS, inv1, inv2, aSin, aTri, aS1, aS2, off, invNorm, pm, ring, pre,
        noise, fb, fbK, subPre, subPost, bias, foldMix, outScale, talkMix;
    g4f ph1, ph2, phS, o2p, fbLp, fbPrev, dcX1, dcY1, postLp;
    g4f ic1[kLeanForm], ic2[kLeanForm], A1[kLeanForm], A2[kLeanForm], G[kLeanForm], GN[kLeanForm];
    g4u rng;
    g4f dA1, dB1, dA2, dB2, dpwK1, dpwK2;     // DPW saw state + gain per oscillator

    for (int j = 0; j < 4; ++j) {
      const Engine &v = *e[j];
      const bool on = live[j];
      w1[j] = v.w1_; w2[j] = v.w2_; wS[j] = v.wSub_;
      inv1[j] = 1.f / (v.w1_ > 1e-6f ? v.w1_ : 1e-6f);
      inv2[j] = 1.f / (v.w2_ > 1e-6f ? v.w2_ : 1e-6f);
      float s = 0.f, t = 0.f, s1 = 0.f, s2 = 0.f, o = 0.5f;
      const float f = v.bFrac_;
      switch (v.bReg_) {
        case 0:  s = 1.f - f; t = f; break;
        case 1:  t = 1.f - f; s1 = f; break;
        case 2:  s1 = 1.f - 2.f * f; s2 = f; o = 0.5f; break;
        default: s1 = -1.f; s2 = 1.f; o = -v.pw_; break;
      }
      // Source level normalisation folded into the waveform weights.
      const float nm = v.srcNorm_;
      aSin[j] = s * nm; aTri[j] = t * nm; aS1[j] = s1 * nm; aS2[j] = s2 * nm; off[j] = o;
      pm[j] = v.pmAmt_; ring[j] = v.ringAmt_; invNorm[j] = 1.f / nm;
      // TALK: GROWL drives the source into a soft clipper (replaces formant FM)
      pre[j] = (mode == kTalk) ? 1.f + 1.5f * v.sGrowl_ : v.preDrive_;
      talkMix[j] = v.sGrowl_ * 2.f > 1.f ? 1.f : v.sGrowl_ * 2.f;   // 0 = clean at GROWL 0
      noise[j] = v.noiseAmt_; fb[j] = v.fbAmt_;
      const float bd = md.makeup * v.pitchComp_ * v.driveGain_ * kShaperIn;   // bank -> shaper gain
      fbK[j] = 1.5f / (bd > 1e-6f ? bd : 1e-6f);   // feedback uses the unscaled bank output
      subPre[j] = md.subPre * v.subScale_;
      subPost[j] = md.subPost * v.subScale_ * v.driveGain_ * kShaperIn;
      bias[j] = 0.3f * v.sDrive_; foldMix[j] = v.sDrive_;
      outScale[j] = v.outComp_ * (1.f + 0.3f * v.sDrive_) * (1.f / kShaperIn) * kOutGain;
      if (!on) outScale[j] = 0.f;
      ph1[j] = v.ph1_; ph2[j] = v.ph2_; phS[j] = v.phSub_; o2p[j] = v.o2Prev_;
      fbLp[j] = v.fbLp_; fbPrev[j] = v.fbPrev_; dcX1[j] = v.dcX1_; dcY1[j] = v.dcY1_; postLp[j] = v.postLp_;
      rng[j] = v.rng_ ? v.rng_ : 0x12345678u;
      dA1[j] = v.dpw_[0]; dB1[j] = v.dpw_[1]; dA2[j] = v.dpw_[2]; dB2[j] = v.dpw_[3];
      // DPW: d/dt of s^2 (s = 2t-1) is 4*dt*s per sample, so scale by 1/(4 dt).
      dpwK1[j] = 0.25f * inv1[j]; dpwK2[j] = 0.25f * inv2[j];
      for (int k = 0; k < kLeanForm; ++k) {
        // Coefficients once per buffer (end-of-block targets), like vox.
        const float g  = on ? v.tgtG_[k] : 0.01f;
        const float kk = on ? v.tgtK_[k] : 1.f;
        const float a1 = 1.f / (1.f + g * (g + kk));
        G[k][j] = g; A1[k][j] = a1; A2[k][j] = g * a1;
        // bank makeup and drive folded into the formant output gains
        GN[k][j] = on ? v.tgtGn_[k] * md.makeup * v.pitchComp_ * v.driveGain_ * kShaperIn : 0.f;
        ic1[k][j] = v.svf_[k].ic1; ic2[k][j] = v.svf_[k].ic2;
      }
    }
    bool useSin = false, useTri = false, useS1 = false, useS2 = false;
    for (int j = 0; j < 4; ++j) {
      if (!live[j]) continue;
      useSin |= aSin[j] != 0.f; useTri |= aTri[j] != 0.f;
      useS1 |= aS1[j] != 0.f;   useS2 |= aS2[j] != 0.f;
    }
    const bool usePm = (mode == kMetal);
    const g4f zero = g4splat(0.f);

    for (uint32_t s = 0; s < n; ++s) {
      g4f p1 = ph1;
      if (usePm) p1 = g4wrap1(p1 + pm * o2p);
      g4f o1 = zero, o2 = zero;
      if (useSin) { o1 += aSin * g4sin2pi(p1); o2 += aSin * g4sin2pi(ph2); }
      if (useTri) { o1 += aTri * g4tri(p1);    o2 += aTri * g4tri(ph2); }
      {
        // DPW saws: parabola of the bipolar phase, differentiated. The state
        // is updated every sample so switching BRIGHT regions never clicks.
        const g4f a1 = 2.f * p1 - 1.f,                 pa1 = a1 * a1;
        const g4f b1 = 2.f * g4wrap1(p1 + off) - 1.f,  pb1 = b1 * b1;
        const g4f a2 = 2.f * ph2 - 1.f,                pa2 = a2 * a2;
        const g4f b2 = 2.f * g4wrap1(ph2 + off) - 1.f, pb2 = b2 * b2;
        const g4f sa1 = (pa1 - dA1) * dpwK1, sb1 = (pb1 - dB1) * dpwK1;
        const g4f sa2 = (pa2 - dA2) * dpwK2, sb2 = (pb2 - dB2) * dpwK2;
        dA1 = pa1; dB1 = pb1; dA2 = pa2; dB2 = pb2;
        if (useS1) { o1 += aS1 * sa1; o2 += aS1 * sa2; }
        if (useS2) { o1 += aS2 * sb1; o2 += aS2 * sb2; }
      }
      o2p = o2;
      const g4f sub = g4sin2pi(phS);

      g4f src;
      if (mode == kMetal) {
        const g4f sum = 0.5f * (o1 + o2);
        src = sum + ring * (1.3f * o1 * o2 * invNorm - sum);   // keep ring level as before
      } else {
        src = 0.5f * (o1 + o2);
      }
      if (mode == kGrowl) {
        src += subPre * g4softclip(2.5f * sub);
        src = g4softclip(src * pre);
        fbLp += 0.3f * (fbPrev - fbLp);
        src += fb * g4softclip(fbK * fbLp);
      } else if (mode == kTalk) {
        src += subPre * sub;
        src += talkMix * (g4softclip(src * pre) - src);
      } else if (mode == kGhost) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const g4u bits = (rng >> 9) | 0x3F800000u;
        g4f u;
        memcpy(&u, &bits, 16);
        src += noise * (2.f * u - 3.f);
      }

      // formant bank: three TPT band-passes, coefficients fixed per buffer
      g4f y = zero;
      for (int k = 0; k < kLeanForm; ++k) {
        const g4f v3 = src - ic2[k];
        const g4f v1 = A1[k] * ic1[k] + A2[k] * v3;
        const g4f v2 = ic2[k] + G[k] * v1;
        ic1[k] = 2.f * v1 - ic1[k];
        ic2[k] = 2.f * v2 - ic2[k];
        y += GN[k] * v1;
      }
      fbPrev = y;                                  // (already bank * drive scaled)
      if (md.subPost > 0.f) y += subPost * sub;
      const g4f x = y;
      g4f sh;
      if (mode == kGrowl) {
        sh = g4softclip(x + bias) - g4softclip(bias);
      } else if (mode == kMetal) {
        const g4f c = g4softclip(x);
        sh = c + foldMix * (g4sin2pi(g4frac(x * 0.25f)) - c);
      } else {
        sh = g4softclip(x);
      }
      sh *= outScale;
      const g4f dc = sh - dcX1 + 0.9985f * dcY1;
      dcX1 = sh; dcY1 = dc;
      postLp += 0.88f * (dc - postLp);
      out[s] = g4softclip(postLp);

      ph1 += w1; ph1 = g4sel(ph1 >= 1.f, ph1 - 1.f, ph1);
      ph2 += w2; ph2 = g4sel(ph2 >= 1.f, ph2 - 1.f, ph2);
      phS += wS; phS = g4sel(phS >= 1.f, phS - 1.f, phS);
    }

    for (int j = 0; j < 4; ++j) {
      if (!live[j]) continue;
      Engine &v = *e[j];
      v.ph1_ = ph1[j]; v.ph2_ = ph2[j]; v.phSub_ = phS[j]; v.o2Prev_ = o2p[j];
      v.fbLp_ = fbLp[j]; v.fbPrev_ = fbPrev[j]; v.dcX1_ = dcX1[j]; v.dcY1_ = dcY1[j]; v.postLp_ = postLp[j];
      v.rng_ = rng[j];
      v.dpw_[0] = dA1[j]; v.dpw_[1] = dB1[j]; v.dpw_[2] = dA2[j]; v.dpw_[3] = dB2[j];
      for (int k = 0; k < kLeanForm; ++k) { v.svf_[k].ic1 = ic1[k][j]; v.svf_[k].ic2 = ic2[k][j]; }
    }
  }
};

}  // namespace fmut

#endif  // FORMANT_LEAN4_H_
