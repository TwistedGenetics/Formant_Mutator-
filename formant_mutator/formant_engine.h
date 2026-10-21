/*
 *  File: formant_engine.h
 *
 *  FORMANT MUTATOR - DSP engine for the Korg NTS-1 mkII (logue SDK 2.0).
 *
 *  This file is deliberately free of SDK dependencies so that exactly the
 *  same code runs on the device (via unit.cc) and in the offline test
 *  harness (test/harness.cc) on a desktop machine.
 *
 *  Signal path (per sample):
 *
 *    osc1 + osc2 (+ sub / ring / PM / noise, depending on mode)
 *      -> harmonic shaping (BRIGHT waveform morph, GROWL pre-drive)
 *      -> 4-band parallel formant bank (TPT state-variable band-passes)
 *      -> nonlinear stage (soft / asymmetric / wavefold, by mode)
 *      -> DC blocker -> gentle 1-pole LPF -> safety soft clip -> out
 *
 *  Real-time rules followed here:
 *    - no heap allocation, no blocking, no file access, no exceptions
 *    - no libm calls at all: the sine table is built by series in init(),
 *      and exp / log / tan / sqrt use small inline approximations
 *    - no trig / exp / pow evaluation inside the per-sample loop
 *    - control-rate (every 32 samples) coefficient updates with per-sample
 *      linear interpolation of filter frequencies and gains
 *    - one division per formant per sample (needed for audio-rate formant FM)
 *    - denormal flushing of all recursive filter states once per control block
 */

#ifndef FORMANT_ENGINE_H_
#define FORMANT_ENGINE_H_

#include <stdint.h>
#include "fm_params.h"

#if defined(__GNUC__)
#define FM_INLINE inline __attribute__((always_inline))
#else
#define FM_INLINE inline
#endif

// 4-wide SIMD path for the formant bank (the four formants of one voice run
// in parallel lanes). Enabled where the compiler targets NEON (microKORG 2,
// Cortex-A7) or SSE (desktop test builds). The NTS-1 mkII's Cortex-M7 has no
// SIMD float unit, so it keeps the scalar path. FM_FORCE_SCALAR forces the
// scalar path anywhere (the test harness builds both and compares them).
#if !defined(FM_FORCE_SCALAR) && defined(__GNUC__) && (defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__SSE2__))
#define FM_SIMD 1
#include <string.h>
typedef float   fm_f4 __attribute__((vector_size(16)));
typedef int32_t fm_i4 __attribute__((vector_size(16)));
#else
#define FM_SIMD 0
#endif

namespace fmut {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const float kFs        = 48000.f;
static const float kInvFs     = 1.f / 48000.f;
static const int   kCtrlBlock = 32;           // control-rate block (1.5 kHz)
static const int   kNumForm   = 4;            // F1..F4
static const int   kNumVowels = 5;            // A E I O U
static const int   kSinBits   = 9;
static const int   kSinSize   = 1 << kSinBits; // 512-point sine table
static const float kPi        = 3.14159265358979f;

enum Mode { kTalk = 0, kGrowl = 1, kMetal = 2, kGhost = 3, kNumModes = 4 };

// ---------------------------------------------------------------------------
// Small math helpers (all branch-light, no libm in the sample loop)
// ---------------------------------------------------------------------------

static FM_INLINE float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

// Pade-style tanh, exact 1.0 at |x| = 3, continuous and monotonic.
static FM_INLINE float tanh_fast(float x) {
  x = clampf(x, -3.f, 3.f);
  const float x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}

// 2^x, |x| < ~20. Integer part via the exponent field; fractional part
// via a 5th-order Taylor series centred on 0.5 (max rel. error ~1e-7).
static FM_INLINE float exp2_fast(float x) {
  int32_t xi = (int32_t)x;
  if (x < (float)xi) --xi;
  const float u = x - (float)xi - 0.5f;               // [-0.5, 0.5)
  const float p = 1.41421356f * (1.f + u * (0.69314718f + u * (0.24022651f + u * (0.05550411f
                    + u * (0.00961813f + u * 0.00133336f)))));
  union { float f; int32_t i; } r;
  r.f = p;
  r.i += xi << 23;
  return r.f;
}

// log2(x) for x > 0, ~5e-3 absolute error (~6 cents). Block rate only.
static FM_INLINE float log2_fast(float x) {
  union { float f; int32_t i; } u;
  u.f = x;
  const float e = (float)(((u.i >> 23) & 255) - 127);
  u.i = (u.i & ~(255 << 23)) | (127 << 23);           // mantissa in [1,2)
  const float m = u.f;
  return e + (-0.34484843f * m + 2.02466578f) * m - 1.67487759f;
}

// sqrt(x) for x > 0 via rsqrt (defined below).
static FM_INLINE float rsqrt_fast(float x);
static FM_INLINE float sqrt_fast(float x) { return x * rsqrt_fast(x); }

// 1/sqrt(x), x > 0, two Newton steps (~1e-6 relative).
static FM_INLINE float rsqrt_fast(float x) {
  union { float f; int32_t i; } u;
  u.f = x;
  u.i = 0x5f3759df - (u.i >> 1);
  float y = u.f;
  y = y * (1.5f - 0.5f * x * y * y);
  y = y * (1.5f - 0.5f * x * y * y);
  return y;
}

// 1/d for d >= 1 without a divide: bit-trick estimate (<= 12 % error) and
// two Newton steps (~2e-4 relative). Divides are slow on the Cortex-A7/M7
// FPUs, and the formant filters need one per filter per sample.
static FM_INLINE float recip_fast(float d) {
  union { float f; int32_t i; } u;
  u.f = d;
  u.i = 0x7EF311C7 - u.i;
  float r = u.f;
  r = r * (2.f - d * r);
  r = r * (2.f - d * r);
  return r;
}

// Cubic soft clipper: x - 4x^3/27 on [-1.5, 1.5], +-1 outside. Slope 1 at
// zero, smooth at the knee, no divide (cheaper than tanh on the target CPUs).
static FM_INLINE float softclip(float x) {
  if (x >= 1.5f) return 1.f;
  if (x <= -1.5f) return -1.f;
  return x - 0.148148148f * x * x * x;
}

// tan(pi * f) for f in [0, 0.23] (Pade [3/2], < 0.05% error in that range).
static FM_INLINE float tanpi_fast(float f) {
  const float t  = kPi * f;
  const float t2 = t * t;
  return t * (15.f - t2) / (15.f - 6.f * t2);
}

// Fold any value into [0,1] with mirror reflection (period 2).
static FM_INLINE float reflect01(float v) {
  float h = v * 0.5f;
  int32_t i = (int32_t)h;
  if (h < (float)i) --i;
  v = v - 2.f * (float)i;                 // now in [0,2)
  return v > 1.f ? 2.f - v : v;
}

static FM_INLINE float wrap01(float p) {
  return p - (float)(int32_t)p + (p < 0.f ? 1.f : 0.f);
}

static FM_INLINE float fabs_(float x) { return x < 0.f ? -x : x; }

// Finite check on the bit pattern, so it survives -ffast-math (which lets the
// compiler assume NaN/Inf never occur and delete ordinary comparisons).
static FM_INLINE bool finite_bits(float x) {
  union { float f; uint32_t u; } v;
  v.f = x;
  return (v.u & 0x7F800000u) != 0x7F800000u;
}

// PolyBLEP residual.
static FM_INLINE float polyblep(float t, float dt, float inv) {
  if (t < dt) {
    t *= inv;
    return t + t - t * t - 1.f;
  } else if (t > 1.f - dt) {
    t = (t - 1.f) * inv;
    return t * t + t + t + 1.f;
  }
  return 0.f;
}

// ---------------------------------------------------------------------------
// Vowel data (bass-voice formant table, after the widely used Csound
// "formant table" values). Index [vowel][formant].
// Gains are given in dB and compressed by kGainCompress at init so that the
// upper formants stay audible on a synth (real voices are far darker).
// ---------------------------------------------------------------------------

static const float kVowelFreq[kNumVowels][kNumForm] = {
  {600.f, 1040.f, 2250.f, 2450.f},  // A
  {400.f, 1620.f, 2400.f, 2800.f},  // E
  {250.f, 1750.f, 2600.f, 3050.f},  // I
  {400.f,  750.f, 2400.f, 2600.f},  // O
  {350.f,  600.f, 2400.f, 2675.f},  // U
};
static const float kVowelBw[kNumVowels][kNumForm] = {
  {60.f, 70.f, 110.f, 120.f},
  {40.f, 80.f, 100.f, 120.f},
  {60.f, 90.f, 100.f, 120.f},
  {40.f, 80.f, 100.f, 120.f},
  {40.f, 80.f, 100.f, 120.f},
};
static const float kVowelDb[kNumVowels][kNumForm] = {
  {0.f,  -7.f,  -9.f,  -9.f},
  {0.f, -12.f,  -9.f, -12.f},
  {0.f, -30.f, -16.f, -22.f},
  {0.f, -11.f, -21.f, -20.f},
  {0.f, -20.f, -32.f, -28.f},
};
static const float kGainCompress = 0.5f;

// Gain staging around the nonlinear stage (see renderBlock).
static const float kShaperIn = 0.6f;   // level into the shaper at DRIVE 0
static const float kOutGain  = 0.75f;  // output level after the shaper

// Per-mode architecture constants.
struct ModeDef {
  float bwMul;        // bandwidth multiplier (lower = sharper resonances)
  float vowelSlew;    // vowel smoothing coefficient per control block
  float keyTrack;     // formant key-tracking (0 = fixed formants)
  float subPre;       // sub osc level into the formant bank
  float subPost;      // clean sub level added after the formant bank
  float makeup;       // output makeup gain
  float pitchTilt;    // level compensation exponent vs note pitch (see controlUpdate)
};

static const ModeDef kModes[kNumModes] = {
  //  bwMul vowelSlew keyTrk subPre subPost makeup pitchTilt
  {   1.0f,  0.06f,    0.0f,  0.30f, 0.00f,  2.6f,  0.45f },  // TALK
  {   0.8f,  0.08f,    0.0f,  0.55f, 0.22f,  2.4f,  0.45f },  // GROWL
  {   0.4f,  0.10f,    0.5f,  0.00f, 0.00f,  3.0f,  0.20f },  // METAL
  {   1.7f,  0.004f,   0.0f,  0.00f, 0.30f,  2.6f,  0.45f },  // GHOST
};

// ---------------------------------------------------------------------------
// Read-only tables shared by every voice (built once, no libm).
// ---------------------------------------------------------------------------

struct Tables {
  float sinTab[kSinSize + 1];
  float vowelGain[kNumVowels][kNumForm];

  void build() {
    // Sine table, each point evaluated directly with a Taylor series on
    // [-pi/2, pi/2] (single precision, no libm, no soft-float doubles).
    for (int i = 0; i <= kSinSize; ++i) {
      float x = 2.f * kPi * (float)(i % kSinSize) / (float)kSinSize;   // [0, 2pi)
      if (x > kPi) x -= 2.f * kPi;                                        // (-pi, pi]
      if (x > 0.5f * kPi) x = kPi - x;                                    // fold to
      else if (x < -0.5f * kPi) x = -kPi - x;                             // [-pi/2, pi/2]
      const float x2 = x * x;
      sinTab[i] = x * (1.f - x2 / 6.f * (1.f - x2 / 20.f * (1.f - x2 / 42.f
                  * (1.f - x2 / 72.f * (1.f - x2 / 110.f * (1.f - x2 / 156.f))))));
    }
    for (int v = 0; v < kNumVowels; ++v)
      for (int f = 0; f < kNumForm; ++f)   // 10^(dB/20) = 2^(dB * log2(10)/20)
        vowelGain[v][f] = exp2_fast(kVowelDb[v][f] * kGainCompress * 0.16609640f);
  }
};

// Per-voice modulation inputs (added to the parameter values, then smoothed).
// Units: vowel/growl/reso/bright/drive in parameter range 0..1, shift in octaves.
enum ModDest { kModVowel = 0, kModGrowl, kModShift, kModReso, kModBright, kModDrive, kNumModDest };

// ---------------------------------------------------------------------------
// TPT (Zavalishin) state-variable band-pass. Stable for any g > 0, k > 0,
// including per-sample coefficient changes.
// ---------------------------------------------------------------------------

struct Svf {
  float ic1 = 0.f, ic2 = 0.f;
  FM_INLINE float bp(float x, float g, float k) {
    const float a1 = recip_fast(1.f + g * (g + k));
    const float a2 = g * a1;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + g * v1;
    ic1 = 2.f * v1 - ic1;
    ic2 = 2.f * v2 - ic2;
    return v1;
  }
  inline void flush() {
    if (fabs_(ic1) < 1e-15f) ic1 = 0.f;
    if (fabs_(ic2) < 1e-15f) ic2 = 0.f;
  }
  inline void reset() { ic1 = ic2 = 0.f; }
};

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

class Engine {
 public:
  Engine() {}

  // Call once before use (not from the audio callback). `t` must already
  // be built and must outlive the engine. `seed` decorrelates voices.
  void init(const Tables *t, uint32_t seed = 0x12345678u) {
    tab_ = t;
    rng_ = seed ? seed : 0x12345678u;
    for (int i = 0; i < kNumModDest; ++i) mod_[i] = 0.f;
    setParam(FM_P_VOWEL,  FM_VOWEL_DEF);
    setParam(FM_P_GROWL,  FM_GROWL_DEF);
    setParam(FM_P_MODE,   FM_MODE_DEF);
    setParam(FM_P_SHIFT,  FM_SHIFT_DEF);
    setParam(FM_P_RESO,   FM_RESO_DEF);
    setParam(FM_P_DETUNE, FM_DETUNE_DEF);
    setParam(FM_P_BRIGHT, FM_BRIGHT_DEF);
    setParam(FM_P_RATE,   FM_RATE_DEF);
    setParam(FM_P_DEPTH,  FM_DEPTH_DEF);
    setParam(FM_P_DRIVE,  FM_DRIVE_DEF);
    // Jump smoothers straight to their targets.
    sVowel_ = tVowel_; sGrowl_ = tGrowl_; sBright_ = tBright_;
    sDrive_ = tDrive_; sShift_ = tShift_;
    sDetune_ = tDetune_; sReso_ = tReso_;
    reset();
  }

  void reset() {
    for (int i = 0; i < kNumForm; ++i) svf_[i].reset();
    ph1_ = 0.f; ph2_ = 0.37f; phSub_ = 0.f;
    lfoPh_ = 0.f; lfoOut_ = 0.f; shTarget_ = 0.f;
    env_ = 0.f; envAttack_ = false;
    fbLp_ = 0.f; fbPrev_ = 0.f;
    dcX1_ = 0.f; dcY1_ = 0.f; postLp_ = 0.f; o2Prev_ = 0.f;
    for (int i = 0; i < kNumForm; ++i) driftPh_[i] = 0.17f * (float)i;
    gates_ = 0;
    firstBlock_ = true;
  }

  // ---- Parameters (raw values exactly as the hardware sends them) --------

  void setParam(uint8_t id, int32_t v) {
    switch (id) {
      case FM_P_VOWEL:  tVowel_ = (float)v * (1.f / 1023.f); break;
      case FM_P_GROWL:  tGrowl_ = (float)v * (1.f / 1023.f); break;
      case FM_P_MODE:   mode_ = (v < 0) ? 0 : (v > 3 ? 3 : (int)v); break;
      case FM_P_SHIFT:  tShift_ = (float)v / 12.f; break;           // octaves
      case FM_P_RESO: {
        tReso_ = (float)v * 0.01f;
      } break;
      case FM_P_DETUNE: tDetune_ = (float)v * 0.01f; break;
      case FM_P_BRIGHT: tBright_ = (float)v * 0.01f; break;
      case FM_P_RATE: {
        const float r = (float)v * 0.01f;
        lfoHz_ = 0.05f * exp2_fast(r * 10.f);                         // 0.05 .. 51 Hz
        if (!splitEnv_) setEnvDecay(r);                               // NTS-1: shared knob
      } break;
      case FM_P_DEPTH: {                                              // -1..1
        const float d = (float)v * 0.01f;
        if (splitEnv_) { lfoDepth_ = d; }                             // bipolar LFO depth
        else { lfoDepth_ = d > 0.f ? d : 0.f; envAmt_ = d < 0.f ? -d : 0.f; }
      } break;
      // ---- extended parameters (microKORG 2 only) ----
      case FM_P_ENV_DECAY: setEnvDecay((float)v * 0.01f); break;
      case FM_P_ENV_AMT:   envAmt_ = (float)v * 0.01f; break;        // -1..1 (neg = formants dip)
      case FM_P_SUB:       subScale_ = (float)v * (1.f / 50.f); break; // 50 = mode default
      case FM_P_VEL_SENS:  velSens_ = (float)v * 0.01f; break;
      case FM_P_DRIVE:  tDrive_ = (float)v * 0.01f; break;
      default: break;
    }
  }

  // microKORG 2: LFO and envelope get their own parameters.
  void setSplitEnv(bool on) { splitEnv_ = on; }

  // Per-voice modulation (see ModDest). Values are offsets, not absolutes.
  inline void setMod(int dest, float v) { if (dest >= 0 && dest < kNumModDest) mod_[dest] = v; }

  // ---- Runtime inputs -----------------------------------------------------

  // w0 = f / fs (cycles per sample). 0.5 is Nyquist.
  inline void setPitch(float w0) {
    if (!finite_bits(w0) || w0 < 1e-6f) w0 = 1e-6f;
    w0_ = w0 < 0.45f ? w0 : 0.45f;
  }
  // Hardware shape LFO in [-1, 1].
  inline void setShapeLfo(float l) { hwLfo_ = l; }

  void noteOn(uint8_t note, uint8_t velo) {
    (void)note;
    vel_ = (1.f - velSens_) + velSens_ * (float)velo * (1.f / 127.f);
    envAttack_ = true;                                  // retrigger formant env
    if (mode_ == kGrowl) lfoPh_ = 0.f;                  // key-synced wobble
    ++gates_;
  }
  void noteOff(uint8_t note) { (void)note; if (gates_ > 0) --gates_; }
  void allNoteOff() { gates_ = 0; }

  // ---- Render ---------------------------------------------------------------

  void process(float *out, uint32_t frames) {
    while (frames) {
      const uint32_t n = frames < (uint32_t)kCtrlBlock ? frames : (uint32_t)kCtrlBlock;
      controlUpdate(n);
      renderBlock(out, n);
      out += n;
      frames -= n;
    }
  }

  // For the test harness.
  int mode() const { return mode_; }

 private:
  friend struct Group4;   // 4-voice SIMD renderer (formant_group4.h, microKORG 2)

  // ---- Control-rate update ------------------------------------------------
  void controlUpdate(uint32_t n) {
    const ModeDef &md = kModes[mode_];

    // Parameter smoothing (one-pole, ~5 ms at 3 kHz control rate).
    const float c = 0.07f;
    sGrowl_   += c * (clampf(tGrowl_  + mod_[kModGrowl],  0.f, 1.f) - sGrowl_);
    sBright_  += c * (clampf(tBright_ + mod_[kModBright], 0.f, 1.f) - sBright_);
    sDrive_   += c * (clampf(tDrive_  + mod_[kModDrive],  0.f, 1.f) - sDrive_);
    sShift_   += c * (tShift_ + mod_[kModShift] - sShift_);
    sDetune_  += c * (tDetune_  - sDetune_);
    sReso_    += c * (clampf(tReso_   + mod_[kModReso],   0.f, 1.f) - sReso_);
    sHwLfo_   += 0.25f * (hwLfo_ - sHwLfo_);
    sVowel_   += md.vowelSlew * (tVowel_ + mod_[kModVowel] - sVowel_);

    const float blockSec = (float)n * kInvFs;

    // --- Internal LFO (shape depends on mode) ---
    float rateHz = lfoHz_;
    if (mode_ == kGhost) rateHz *= 0.25f;
    lfoPh_ += rateHz * blockSec;
    if (lfoPh_ >= 1.f) {
      lfoPh_ -= (float)(int32_t)lfoPh_;
      shTarget_ = randBipolar();
    }
    float lfo;
    switch (mode_) {
      case kTalk:  lfo = 1.f - 4.f * fabs_(lfoPh_ - 0.5f); break;             // triangle
      case kMetal: lfoOut_ += 0.35f * (shTarget_ - lfoOut_); lfo = lfoOut_; break; // S&H
      default:     lfo = sinLookup(lfoPh_); break;                             // sine
    }

    // --- Formant envelope (attack ~2 ms, exponential decay) ---
    const float envDecayBlock = powBlock(envDecay_, n);
    if (envAttack_) {
      env_ += blockSec * 500.f;
      if (env_ >= 1.f) { env_ = 1.f; envAttack_ = false; }
    } else {
      env_ *= envDecayBlock;
      if (env_ < 1e-12f) env_ = 0.f;                     // never go denormal
    }

    // --- Modulation routing ---
    float vowelMod = 0.f, shiftMod = 0.f;
    if (lfoDepth_ != 0.f) {
      vowelMod = lfoDepth_ * 0.5f * lfo;
      if (mode_ == kMetal) shiftMod = lfoDepth_ * 0.5f * lfo;    // robot jumps
      if (mode_ == kGrowl) shiftMod = lfoDepth_ * 0.15f * lfo;   // wobble
    }
    if (envAmt_ != 0.f) {
      // Note-on "bark": formants jump up (up to +1.5 oct) and fall back,
      // with a small vowel push for extra articulation.
      const float a = envAmt_ * env_ * vel_;
      vowelMod += a * 0.15f;
      shiftMod += a * 1.5f;
    }

    const float v = reflect01(sVowel_ + sHwLfo_ + vowelMod);
    const float pos = v * (float)(kNumVowels - 1);
    int vi = (int)pos;
    if (vi > kNumVowels - 2) vi = kNumVowels - 2;
    const float vf = pos - (float)vi;

    // Global formant frequency multiplier.
    float oct = sShift_ + shiftMod;
    if (md.keyTrack > 0.f) {
      // track the note relative to C3 (130.81 Hz)
      oct += md.keyTrack * log2_fast(w0_ * kFs * (1.f / 130.81f));
    }
    const float fmul = exp2_fast(clampf(oct, -4.f, 4.f));

    // Pitch level compensation. More harmonics land inside the formant bands
    // as the note rises, so the bank output grows ~2.7 dB/octave (measured).
    // Normalise to C3 so every note hits the shaper equally hard.
    {
      const float octFromC3 = clampf(log2_fast(w0_ * kFs * (1.f / 130.81f)), -3.f, 4.f);
      const float target = exp2_fast(-md.pitchTilt * octFromC3);
      pitchComp_ += 0.3f * (target - pitchComp_);
    }

    // GHOST: independent slow drift per formant (choir-like beating).
    float drift[kNumForm] = {1.f, 1.f, 1.f, 1.f};
    if (mode_ == kGhost) {
      static const float kDriftHz[kNumForm] = {0.13f, 0.21f, 0.31f, 0.17f};
      const float amt = 0.03f + 0.07f * sGrowl_;
      for (int i = 0; i < kNumForm; ++i) {
        driftPh_[i] = wrap01(driftPh_[i] + kDriftHz[i] * blockSec);
        drift[i] = 1.f + amt * sinLookup(driftPh_[i]);
      }
    }

    // Resonance: GROWL knob also tightens resonances a little.
    const float bwScale = 2.5f * exp2_fast(sReso_ * -3.64385619f)   // 2.5 * 0.08^reso
                          * md.bwMul * (1.f - 0.35f * sGrowl_);
    const float invN = 1.f / (float)n;
    // Pitch-relative bandwidth floor: a formant narrower than the harmonic
    // spacing would fall between harmonics at bass pitches and the vowel
    // would vanish. The floor keeps every formant catching 1-2 harmonics;
    // high RESO lowers it so formants can still ring.
    const float bwFloorHz = w0_ * kFs * (1.3f - 1.0f * sReso_) * md.bwMul;

    for (int i = 0; i < kNumForm; ++i) {
      const float f0 = kVowelFreq[vi][i] + vf * (kVowelFreq[vi + 1][i] - kVowelFreq[vi][i]);
      const float bw = kVowelBw[vi][i]   + vf * (kVowelBw[vi + 1][i]   - kVowelBw[vi][i]);
      const float gn = tab_->vowelGain[vi][i] + vf * (tab_->vowelGain[vi + 1][i] - tab_->vowelGain[vi][i]);
      const float fc = clampf(f0 * fmul * drift[i], 40.f, 11000.f);
      const float g  = tanpi_fast(fc * kInvFs);
      // Constant-Q: Q from the table at the unshifted frequency.
      const float kq = bw * bwScale / f0;
      const float kf = bwFloorHz / fc;
      const float k  = clampf(kq > kf ? kq : kf, 0.004f, 2.f);
      // Normalised BP (k * v1) has unity peak gain; narrow bands collect
      // less energy, so add partial makeup.
      const float makeup = rsqrt_fast(clampf(k * 8.f, 0.05f, 4.f));
      tgtG_[i]  = g;
      tgtK_[i]  = k;
      tgtGn_[i] = gn * k * makeup;
      if (firstBlock_) { curG_[i] = g; curK_[i] = k; curGn_[i] = tgtGn_[i]; }
      incG_[i]  = (tgtG_[i]  - curG_[i])  * invN;
      incK_[i]  = (tgtK_[i]  - curK_[i])  * invN;
      incGn_[i] = (tgtGn_[i] - curGn_[i]) * invN;
    }
    firstBlock_ = false;

    // --- Oscillator setup ---
    const float w = w0_;
    if (mode_ == kMetal) {
      // inharmonic ratio 1.0 .. 4.5, slightly off-integer
      w2_ = w * (1.0f + 3.5f * sDetune_ + 0.013f);
    } else {
      const float cents = 50.f * sDetune_ * sDetune_ + 0.5f;  // 0.5 .. 50.5 c
      w2_ = w * exp2_fast(cents * (1.f / 1200.f));
      w1_ = w * exp2_fast(-cents * (0.25f / 1200.f));
    }
    if (mode_ == kMetal) w1_ = w;
    wSub_ = w * 0.5f;
    w1_ = clampf(w1_, 0.f, 0.45f);
    w2_ = clampf(w2_, 0.f, 0.45f);

    // Brightness morph (sine->tri->saw->square->narrow pulse). The sine
    // segment is kept short: a pure sine has no harmonics for the formants
    // to shape, so most of the knob travel is spent on rich waveforms.
    //   0-10 % sine->tri | 10-45 % tri->saw | 45-75 % saw->square | 75-100 % pulse width
    {
      const float b = sBright_;
      if (b < 0.10f)      { bReg_ = 0; bFrac_ = b * 10.f; }
      else if (b < 0.45f) { bReg_ = 1; bFrac_ = (b - 0.10f) * (1.f / 0.35f); }
      else if (b < 0.75f) { bReg_ = 2; bFrac_ = (b - 0.45f) * (1.f / 0.30f); }
      else                { bReg_ = 3; bFrac_ = (b - 0.75f) * 4.f; }
      pw_ = (bReg_ == 3) ? (0.5f - 0.42f * bFrac_) : 0.5f;
      // Equal-loudness source: normalise every waveform to the RMS of a saw
      // (sine .707, triangle/saw .577, square 1.0, pulse 2*sqrt(pw(1-pw))),
      // so BRIGHT changes tone, not level (and not how hard the shaper is hit).
      float rmsW;
      switch (bReg_) {
        case 0:  rmsW = 0.7071f + bFrac_ * (0.5774f - 0.7071f); break;
        case 1:  rmsW = 0.5774f; break;
        case 2:  rmsW = 0.5774f + bFrac_ * (1.0f - 0.5774f); break;
        default: rmsW = 2.f * sqrt_fast(pw_ * (1.f - pw_)); break;
      }
      srcNorm_ = 0.5774f / rmsW;
    }

    // Drive / shaper settings.
    const float d = sDrive_;
    driveGain_ = 1.f + 15.f * d * d;
    outComp_   = 1.f / (1.f + 1.2f * d);

    // Growl macro per mode.
    const float gr = sGrowl_;
    switch (mode_) {
      case kTalk:  fmAmt_ = 0.30f * gr; fbAmt_ = 0.f;        preDrive_ = 1.f;            ringAmt_ = 0.f; pmAmt_ = 0.f; noiseAmt_ = 0.f; break;
      case kGrowl: fmAmt_ = 0.6f * gr;  fbAmt_ = 0.3f * gr;  preDrive_ = 1.f + 4.f * gr;  ringAmt_ = 0.f; pmAmt_ = 0.f; noiseAmt_ = 0.f; break;
      case kMetal: fmAmt_ = 0.25f * gr; fbAmt_ = 0.f;        preDrive_ = 1.f;            ringAmt_ = gr;  pmAmt_ = 0.35f * gr; noiseAmt_ = 0.f; break;
      default:     fmAmt_ = 0.f;        fbAmt_ = 0.f;        preDrive_ = 1.f;            ringAmt_ = 0.f; pmAmt_ = 0.f; noiseAmt_ = 0.5f * gr; break;
    }

    // Self-heal: if any recursive state ever became non-finite (should not
    // happen, but a bad pitch from the host must never break the whole mix),
    // clear the voice's filters instead of outputting NaN forever.
    {
      float chk = fbLp_ + dcY1_ + postLp_;
      for (int i = 0; i < kNumForm; ++i) chk += svf_[i].ic1 + svf_[i].ic2;
      if (!finite_bits(chk)) {
        for (int i = 0; i < kNumForm; ++i) svf_[i].reset();
        fbLp_ = fbPrev_ = dcX1_ = dcY1_ = postLp_ = o2Prev_ = 0.f;
      }
    }
    for (int i = 0; i < kNumForm; ++i) svf_[i].flush();
    if (fabs_(fbLp_) < 1e-15f) fbLp_ = 0.f;
    if (fabs_(dcY1_) < 1e-15f) dcY1_ = 0.f;
    if (fabs_(postLp_) < 1e-15f) postLp_ = 0.f;
  }

  // ---- Per-sample render ----------------------------------------------------
  void renderBlock(float *out, uint32_t n) {
    const ModeDef &md = kModes[mode_];
    const int mode = mode_;
    const float w1 = w1_, w2 = w2_, wSub = wSub_;
    const float inv1 = 1.f / (w1 > 1e-6f ? w1 : 1e-6f);   // polyBLEP reciprocals,
    const float inv2 = 1.f / (w2 > 1e-6f ? w2 : 1e-6f);   // once per block
#if FM_SIMD
    fm_f4 cG, cK, cGn, iG, iK, iGn, ic1, ic2;
    memcpy(&cG, curG_, 16);  memcpy(&cK, curK_, 16);  memcpy(&cGn, curGn_, 16);
    memcpy(&iG, incG_, 16);  memcpy(&iK, incK_, 16);  memcpy(&iGn, incGn_, 16);
    for (int i = 0; i < kNumForm; ++i) { ic1[i] = svf_[i].ic1; ic2[i] = svf_[i].ic2; }
#endif

    for (uint32_t s = 0; s < n; ++s) {
      // --- Oscillators ---
      float p1 = ph1_;
      if (pmAmt_ > 0.f) p1 = wrap01(p1 + pmAmt_ * o2Prev_);
      const float o1 = shapeOsc(p1, w1, inv1);
      const float o2 = shapeOsc(ph2_, w2, inv2);
      o2Prev_ = o2;
      const float subSin = sinLookup(phSub_);

      float src;
      if (mode == kMetal) {
        const float sum  = 0.5f * (o1 + o2);
        const float ring = 1.3f * o1 * o2;
        src = sum + ringAmt_ * (ring - sum);
      } else {
        src = 0.5f * (o1 + o2);
      }
      src *= srcNorm_;

      if (mode == kGrowl) {
        // rounded-square sub, then pre-drive into the formants
        src += md.subPre * subScale_ * softclip(2.5f * subSin);
        src = softclip(src * preDrive_);
      } else if (md.subPre > 0.f) {
        src += md.subPre * subScale_ * subSin;
      }
      if (noiseAmt_ > 0.f) src += noiseAmt_ * randBipolar();

      // Feedback growl (GROWL): low-passed formant output fed back into the
      // bank through tanh. Loop gain is kept below 1 (0.3 x 1.5 x bank gain)
      // so it thickens and "barks" without tipping into self-oscillation.
      if (fbAmt_ > 0.f) {
        fbLp_ += 0.3f * (fbPrev_ - fbLp_);
        src += fbAmt_ * softclip(1.5f * fbLp_);
      }

      // --- Formant bank ---
      // Audio-rate formant FM. TALK uses a sine at the note frequency (smooth
      // "yoi" buzz); GROWL the sub-octave (snarl); METAL osc 1 (clang).
      const float modSig = (mode == kGrowl) ? subSin : (mode == kTalk ? sinLookup(ph1_) : o1);
      const float fm = clampf(1.f + fmAmt_ * modSig, 0.3f, 2.2f);

#if FM_SIMD
      float y;
      {
        // Same maths as Svf::bp, four formants at once.
        cG = cG + iG; cK = cK + iK; cGn = cGn + iGn;
        fm_f4 g = cG * fm;
        g = g < 0.002f ? (fm_f4){0.002f, 0.002f, 0.002f, 0.002f} : g;
        g = g > 3.f ? (fm_f4){3.f, 3.f, 3.f, 3.f} : g;
        const fm_f4 d = 1.f + g * (g + cK);
        fm_f4 r = (fm_f4)(0x7EF311C7 - (fm_i4)d);         // recip_fast, 4 lanes
        r = r * (2.f - d * r);
        r = r * (2.f - d * r);
        const fm_f4 v3 = src - ic2;
        const fm_f4 v1 = r * ic1 + (g * r) * v3;
        const fm_f4 v2 = ic2 + g * v1;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        const fm_f4 o = cGn * v1;
        y = (o[0] + o[1]) + (o[2] + o[3]);
      }
#else
      float y = 0.f;
      for (int i = 0; i < kNumForm; ++i) {
        curG_[i]  += incG_[i];
        curK_[i]  += incK_[i];
        curGn_[i] += incGn_[i];
        const float g = clampf(curG_[i] * fm, 0.002f, 3.f);
        y += curGn_[i] * svf_[i].bp(src, g, curK_[i]);
      }
#endif
      fbPrev_ = y;
      y *= md.makeup * pitchComp_;

      // Clean sub after the bank (weight / hollow sub).
      if (md.subPost > 0.f) y += md.subPost * subScale_ * subSin;

      // --- Nonlinear stage ---
      // Headroom: at DRIVE 0 the shaper sees peaks of ~0.5 and stays nearly
      // linear; DRIVE then pushes up to ~10x for deliberate distortion.
      const float x = y * driveGain_ * kShaperIn;
      float sh;
      switch (mode) {
        case kGrowl: {                         // asymmetric
          const float bias = 0.3f * sDrive_;
          sh = softclip(x + bias) - softclip(bias);
        } break;
        case kMetal: {                         // sine wavefold blended with tanh
          const float fold = sinLookup(wrap01(x * 0.25f));
          sh = softclip(x) + sDrive_ * (fold - softclip(x));
        } break;
        default:                               // soft saturation
          sh = softclip(x);
      }
      sh *= outComp_ * (1.f + 0.3f * sDrive_) * (1.f / kShaperIn) * kOutGain;

      // DC blocker (~11 Hz) and gentle post LPF (~16 kHz) against shaper aliasing.
      const float dc = sh - dcX1_ + 0.9985f * dcY1_;
      dcX1_ = sh; dcY1_ = dc;
      postLp_ += 0.88f * (dc - postLp_);

      // Safety limiter: transparent below 0.6, soft above (|out| < 1 always).
      const float o = postLp_;
      const float ao = fabs_(o);
      out[s] = ao < 0.6f ? o : (o > 0.f ? 1.f : -1.f) * (0.6f + 0.38f * softclip((ao - 0.6f) * 2.5f));

      // --- Phase advance ---
      ph1_ += w1;   if (ph1_ >= 1.f) ph1_ -= 1.f;
      ph2_ += w2;   if (ph2_ >= 1.f) ph2_ -= 1.f;
      phSub_ += wSub; if (phSub_ >= 1.f) phSub_ -= 1.f;
    }
#if FM_SIMD
    memcpy(curG_, &cG, 16);  memcpy(curK_, &cK, 16);  memcpy(curGn_, &cGn, 16);
    for (int i = 0; i < kNumForm; ++i) { svf_[i].ic1 = ic1[i]; svf_[i].ic2 = ic2[i]; }
#endif
  }

  // Waveform morph with polyBLEP anti-aliasing on saw and pulse.
  FM_INLINE float shapeOsc(float t, float dt, float inv) const {
    const float f = bFrac_;
    switch (bReg_) {
      case 0: {                                        // sine -> triangle
        const float sn = sinLookup(t);
        const float tr = triOf(t);
        return sn + f * (tr - sn);
      }
      case 1: {                                        // triangle -> saw
        const float tr = triOf(t);
        const float sw = sawOf(t, dt, inv);
        return tr + f * (sw - tr);
      }
      case 2: {                                        // saw -> square
        // square = saw(t + 1/2) - saw(t), so the blend needs only two
        // band-limited saws: (1 - 2f) saw(t) + f saw(t + 1/2).
        const float a = sawOf(t, dt, inv);
        float t2 = t + 0.5f; if (t2 >= 1.f) t2 -= 1.f;
        return (1.f - 2.f * f) * a + f * sawOf(t2, dt, inv);
      }
      default: {                                       // square -> narrow pulse
        // DC-free pulse of width pw as the difference of two saws.
        float t2 = t - pw_; if (t2 < 0.f) t2 += 1.f;
        return sawOf(t2, dt, inv) - sawOf(t, dt, inv);
      }
    }
  }
  static FM_INLINE float triOf(float t) {
    // sine-phase triangle: 0 at t=0, +1 at 0.25, -1 at 0.75
    float x = t + 0.25f; if (x >= 1.f) x -= 1.f;
    return 1.f - 4.f * fabs_(x - 0.5f);
  }
  static FM_INLINE float sawOf(float t, float dt, float inv) {
    return 2.f * t - 1.f - polyblep(t, dt, inv);
  }

  FM_INLINE float sinLookup(float ph) const {
    const float x = ph * (float)kSinSize;
    int32_t i = (int32_t)x;
    const float fr = x - (float)i;
    i &= (kSinSize - 1);
    const float *t = tab_->sinTab;
    return t[i] + fr * (t[i + 1] - t[i]);
  }

  FM_INLINE float randBipolar() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return (float)(int32_t)rng_ * (1.f / 2147483648.f);
  }

  // Envelope decay time 15 ms (r=1) .. 1.5 s (r=0): exp(-1/(t*fs)),
  // via Taylor series (argument <= 0.0014, error < 1e-12).
  void setEnvDecay(float r) {
    const float tsec = 0.015f * exp2_fast((1.f - r) * 6.64385619f);
    const float x = 1.f / (tsec * kFs);
    envDecay_ = 1.f - x * (1.f - x * (0.5f - x * (1.f / 6.f)));
  }

  // c^n for small integer n (block length), no powf.
  static FM_INLINE float powBlock(float c, uint32_t n) {
    float r = 1.f;
    while (n) { if (n & 1u) r *= c; c *= c; n >>= 1u; }
    return r;
  }

  // ---- State ---------------------------------------------------------------
  const Tables *tab_ = nullptr;
  Svf   svf_[kNumForm];
  float mod_[kNumModDest];

  // targets (written by setParam) and smoothed values
  float tVowel_ = 0.f, tGrowl_ = 0.f, tBright_ = 0.5f, tDrive_ = 0.f;
  float tShift_ = 0.f, tDetune_ = 0.f, tReso_ = 0.5f;
  float sVowel_ = 0.f, sGrowl_ = 0.f, sBright_ = 0.5f, sDrive_ = 0.f;
  float sShift_ = 0.f, sDetune_ = 0.f, sHwLfo_ = 0.f, sReso_ = 0.5f;
  float lfoDepth_ = 0.f, envAmt_ = 0.f, lfoHz_ = 1.f, envDecay_ = 0.999f;
  float subScale_ = 1.f, velSens_ = 0.65f;
  bool  splitEnv_ = false;
  int   mode_ = 0;

  // runtime
  float w0_ = 110.f / 48000.f, hwLfo_ = 0.f, vel_ = 1.f;
  float ph1_ = 0.f, ph2_ = 0.f, phSub_ = 0.f, o2Prev_ = 0.f;
  float w1_ = 0.f, w2_ = 0.f, wSub_ = 0.f;
  int   bReg_ = 2; float bFrac_ = 0.f, pw_ = 0.5f;
  float lfoPh_ = 0.f, lfoOut_ = 0.f, shTarget_ = 0.f;
  float env_ = 0.f; bool envAttack_ = false;
  float driftPh_[kNumForm];
  float fmAmt_ = 0.f, fbAmt_ = 0.f, preDrive_ = 1.f, ringAmt_ = 0.f, pmAmt_ = 0.f, noiseAmt_ = 0.f;
  float driveGain_ = 1.f, outComp_ = 1.f;
  float fbLp_ = 0.f, fbPrev_ = 0.f, dcX1_ = 0.f, dcY1_ = 0.f, postLp_ = 0.f;
  float tgtG_[kNumForm], tgtK_[kNumForm], tgtGn_[kNumForm];
  float curG_[kNumForm], curK_[kNumForm], curGn_[kNumForm];
  float incG_[kNumForm], incK_[kNumForm], incGn_[kNumForm];
  bool  firstBlock_ = true;
  float srcNorm_ = 1.f, pitchComp_ = 1.f;
  uint32_t rng_ = 1u;
  int   gates_ = 0;
};

}  // namespace fmut

#endif  // FORMANT_ENGINE_H_
