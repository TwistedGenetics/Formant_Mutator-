/*
 *  FORMANT MUTATOR - offline test harness.
 *
 *  Compiles the exact same DSP engine (formant_engine.h) used by the NTS-1
 *  mkII unit for a desktop machine, drives it the way the hardware does
 *  (raw parameter values, note on/off, render callbacks of N frames, pitch
 *  updated per callback) and:
 *    - renders the eight test patches to WAV files for listening
 *    - runs automated checks (pitch accuracy, vowel identity, click metric
 *      during vowel sweeps, extreme settings, long random soak, helpers)
 *
 *  What it does NOT model: the NTS-1 mkII amp EG, filter, effects, and the
 *  real Cortex-M7 timing. A simple linear-attack/exponential-release gate is
 *  applied to the patch renders only so that riffs sound like notes.
 *
 *  Build:  make -C test        Run: ./test/harness [outdir]
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>
#include <complex>

#include "../formant_mutator/fm_params.h"
#include "../formant_mutator/formant_engine.h"

using fmut::Engine;

static const int kFs = 48000;
static int g_failures = 0;
static FILE *g_report = nullptr;

#define REPORT(...) do { printf(__VA_ARGS__); if (g_report) fprintf(g_report, __VA_ARGS__); } while (0)
#define CHECK(cond, ...) do { if (!(cond)) { ++g_failures; REPORT("  FAIL: "); REPORT(__VA_ARGS__); REPORT("\n"); } } while (0)

// ---------------------------------------------------------------------------
// WAV writer (16-bit mono PCM)
// ---------------------------------------------------------------------------
static void writeWav(const std::string &path, const std::vector<float> &x) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); return; }
  const uint32_t n = (uint32_t)x.size();
  const uint32_t dataBytes = n * 2;
  auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(1); w32(kFs); w32(kFs * 2); w16(2); w16(16);
  fwrite("data", 1, 4, f); w32(dataBytes);
  for (float s : x) {
    float c = s < -1.f ? -1.f : (s > 1.f ? 1.f : s);
    int16_t v = (int16_t)lrintf(c * 32767.f);
    fwrite(&v, 2, 1, f);
  }
  fclose(f);
}

// ---------------------------------------------------------------------------
// Hardware-like driver
// ---------------------------------------------------------------------------
struct Patch {
  const char *name;
  const char *file;
  int32_t p[FM_NUM_PARAMS];   // VOWEL GROWL MODE SHIFT RESO DETUNE BRIGHT RATE DEPTH DRIVE
};

struct Driver {
  Engine e;
  float note = 48.f;          // fractional MIDI note
  float hwLfo = 0.f;
  uint32_t block = 64;
  Driver() { e.init(); }
  void apply(const Patch &pt) { for (int i = 0; i < FM_NUM_PARAMS; ++i) e.setParam(i, pt.p[i]); }
  // render `frames` samples in hardware-sized callbacks
  void render(float *out, uint32_t frames) {
    while (frames) {
      const uint32_t n = frames < block ? frames : block;
      const float hz = 440.f * powf(2.f, (note - 69.f) / 12.f);
      e.setPitch(hz / (float)kFs);
      e.setShapeLfo(hwLfo);
      e.process(out, n);
      out += n; frames -= n;
    }
  }
};

// ---------------------------------------------------------------------------
// Analysis helpers
// ---------------------------------------------------------------------------
static bool allFinite(const std::vector<float> &x) {
  for (float s : x) if (!std::isfinite(s)) return false;
  return true;
}
static float peakOf(const std::vector<float> &x, size_t a = 0, size_t b = 0) {
  if (!b) b = x.size();
  float p = 0.f; for (size_t i = a; i < b; ++i) p = std::max(p, std::fabs(x[i])); return p;
}
static float rmsOf(const std::vector<float> &x, size_t a = 0, size_t b = 0) {
  if (!b) b = x.size();
  double s = 0; for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i];
  return (float)std::sqrt(s / (double)(b - a));
}
static float meanOf(const std::vector<float> &x, size_t a, size_t b) {
  double s = 0; for (size_t i = a; i < b; ++i) s += x[i]; return (float)(s / (double)(b - a));
}
static float db(float x) { return 20.f * log10f(std::max(x, 1e-9f)); }

// Naive DFT magnitude at a set of bins is too slow; use a radix-2 FFT.
static void fft(std::vector<std::complex<double>> &a) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2 * M_PI / (double)len;
    const std::complex<double> wl(cos(ang), sin(ang));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1);
      for (size_t j = 0; j < len / 2; ++j) {
        auto u = a[i + j], v = a[i + j + len / 2] * w;
        a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
      }
    }
  }
}
// Magnitude spectrum (Hann) of x[start .. start+N)
static std::vector<double> spectrum(const std::vector<float> &x, size_t start, size_t N) {
  std::vector<std::complex<double>> a(N);
  for (size_t i = 0; i < N; ++i) {
    const double w = 0.5 - 0.5 * cos(2 * M_PI * (double)i / (double)(N - 1));
    a[i] = w * x[start + i];
  }
  fft(a);
  std::vector<double> m(N / 2);
  for (size_t i = 0; i < N / 2; ++i) m[i] = std::abs(a[i]);
  return m;
}

// Fundamental estimate: harmonic-sum search around the expected f0, refined
// by parabolic interpolation on the strongest of the first harmonics.
static double estimateF0(const std::vector<float> &x, double expect) {
  const size_t N = 1 << 17;                      // 2.7 s, 0.37 Hz bins
  auto m = spectrum(x, x.size() - N, N);
  const double binHz = (double)kFs / (double)N;
  // search candidates within +-1 semitone, step 0.05 Hz
  double best = expect, bestScore = -1;
  for (double f = expect * 0.944; f <= expect * 1.059; f += expect * 0.0002) {
    double s = 0;
    for (int h = 1; h <= 8; ++h) {
      const double fh = f * h; if (fh > 20000) break;
      const size_t b = (size_t)llround(fh / binHz);
      s += m[b];
    }
    if (s > bestScore) { bestScore = s; best = f; }
  }
  // refine on the strongest harmonic among the first 6
  int hb = 1; double hm = 0;
  for (int h = 1; h <= 6; ++h) {
    const size_t b = (size_t)llround(best * h / binHz);
    if (b + 1 < m.size() && m[b] > hm) { hm = m[b]; hb = h; }
  }
  size_t b = (size_t)llround(best * hb / binHz);
  for (size_t k = b - 3; k <= b + 3; ++k) if (m[k] > m[b]) b = k;
  const double y0 = log(m[b - 1] + 1e-12), y1 = log(m[b] + 1e-12), y2 = log(m[b + 1] + 1e-12);
  const double d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
  return ((double)b + d) * binHz / (double)hb;
}

// Spectral envelope peak search: energy-weighted band energies.
static double bandEnergy(const std::vector<double> &m, double lo, double hi, size_t N) {
  const double binHz = (double)kFs / (double)N;
  double s = 0;
  for (size_t b = (size_t)(lo / binHz); b <= (size_t)(hi / binHz) && b < m.size(); ++b) s += m[b] * m[b];
  return s;
}

// Click metric: peak of the 2nd difference (a crude high-pass), normalised
// by RMS. Sudden steps/clicks produce outliers.
static float clickMetric(const std::vector<float> &x, size_t a, size_t b) {
  float p = 0.f;
  for (size_t i = a + 2; i < b; ++i) p = std::max(p, std::fabs(x[i] - 2.f * x[i - 1] + x[i - 2]));
  return p / std::max(rmsOf(x, a, b), 1e-6f);
}

// ---------------------------------------------------------------------------
// Patches (raw hardware values)
// VOWEL GROWL MODE SHIFT RESO DETUNE BRIGHT RATE DEPTH DRIVE
// ---------------------------------------------------------------------------
static const Patch kPatches[] = {
  {"Talking Reese Bass",      "01_talking_reese_bass",    { 300, 350, 0,  -5, 60, 45, 50, 28,  60, 30}},
  {"Dark Robot",              "02_dark_robot",            { 512, 450, 2, -12, 70, 30, 60, 55,  50, 35}},
  {"Metallic Neuro Growl",    "03_metallic_neuro_growl",  { 250, 800, 1,  -2, 78, 22, 62, 47,  75, 60}},
  {"Ghost Choir",             "04_ghost_choir",           { 600, 300, 3,   0, 45, 35, 20, 30,  40, 10}},
  {"Jungle Vocal Stab",       "05_jungle_vocal_stab",     { 100, 250, 0,   3, 65, 15, 55, 70, -80, 25}},
  {"Sci-Fi Drone",            "06_scifi_drone",           { 700, 600, 2,  -7, 85, 62, 40, 35,  80, 20}},
  {"Hollow Sub Bass",         "07_hollow_sub_bass",       {1023,   0, 3, -12, 30,  5, 10, 20,   0, 15}},
  {"Aggressive Formant Lead", "08_aggressive_formant_lead",{ 450, 600, 1,   5, 70, 40, 85, 62,  45, 75}},
};
static const int kNumPatches = sizeof(kPatches) / sizeof(kPatches[0]);

struct Ev { float beat; float len; int note; int vel; };

// Simple riff renderer with a harness-only amp gate.
static std::vector<float> renderRiff(const Patch &pt, const std::vector<Ev> &evs, float bpm,
                                     float totalBeats, float atk = 0.003f, float rel = 0.05f,
                                     float vowelSweepFrom = -1.f, float vowelSweepTo = -1.f) {
  Driver d; d.apply(pt);
  const double spb = 60.0 / bpm;
  const size_t total = (size_t)(totalBeats * spb * kFs);
  std::vector<float> out(total, 0.f);
  const uint32_t blk = 64;
  float amp = 0.f; int gate = 0; size_t evi = 0;
  std::vector<float> tmp(blk);
  const float atkInc = 1.f / (atk * kFs);
  const float relC = expf(-1.f / (rel * kFs));
  std::vector<int> offAt(evs.size());
  for (size_t pos = 0; pos < total; pos += blk) {
    const double t = (double)pos / kFs;
    // events
    while (evi < evs.size() && evs[evi].beat * spb <= t) {
      d.note = (float)evs[evi].note;
      d.e.noteOn((uint8_t)evs[evi].note, (uint8_t)evs[evi].vel);
      gate = 1; ++evi;
    }
    if (evi > 0) {
      const Ev &cur = evs[evi - 1];
      if (t >= (cur.beat + cur.len) * spb && gate) { d.e.noteOff((uint8_t)cur.note); gate = 0; }
    }
    if (vowelSweepFrom >= 0.f) {
      const float u = (float)pos / (float)total;
      d.e.setParam(FM_P_VOWEL, (int32_t)lrintf(vowelSweepFrom + u * (vowelSweepTo - vowelSweepFrom)));
    }
    const uint32_t n = (uint32_t)std::min<size_t>(blk, total - pos);
    d.render(tmp.data(), n);
    for (uint32_t i = 0; i < n; ++i) {
      if (gate) amp = std::min(1.f, amp + atkInc); else amp *= relC;
      out[pos + i] = tmp[i] * amp;
    }
  }
  return out;
}

static void renderPatches(const std::string &dir) {
  REPORT("\n== Patch renders (harness amp gate applied; unit output itself is ungated) ==\n");
  REPORT("%-26s %8s %8s %8s %10s\n", "patch", "peak", "peakdB", "rmsdB", "clip>=.99");
  for (int i = 0; i < kNumPatches; ++i) {
    const Patch &pt = kPatches[i];
    std::vector<Ev> ev;
    float bpm = 172.f, beats = 16.f;
    float sweepA = -1, sweepB = -1;
    switch (i) {
      case 0: // reese: long rolling notes, vowel sweep across the phrase
        ev = {{0, 1.5f, 28, 110}, {2, 1.5f, 28, 100}, {4, 0.75f, 31, 110}, {5, 0.75f, 33, 100},
              {6, 1.75f, 28, 120}, {8, 1.5f, 26, 110}, {10, 1.5f, 28, 100}, {12, 3.5f, 23, 127}};
        sweepA = 100; sweepB = 900; break;
      case 1: // dark robot: off-beat stabs
        ev = {{0, 0.4f, 33, 110}, {0.75f, 0.25f, 33, 90}, {1.5f, 0.4f, 36, 110}, {2.5f, 0.4f, 31, 120},
              {4, 0.4f, 33, 110}, {4.75f, 0.25f, 33, 90}, {5.5f, 0.4f, 38, 110}, {6.5f, 1.2f, 28, 127},
              {8, 0.4f, 33, 110}, {9.5f, 0.4f, 36, 110}, {10.5f, 0.4f, 31, 120}, {12, 3.5f, 28, 127}};
        break;
      case 2: // neuro growl: 16th pattern
        for (int k = 0; k < 16; ++k) {
          static const int pat[16] = {28, 0, 28, 31, 0, 28, 0, 33, 28, 0, 26, 0, 28, 31, 0, 35};
          if (pat[k]) ev.push_back({k * 0.5f, 0.42f, pat[k], 100 + (k % 4 == 0 ? 27 : 0)});
        }
        { const size_t n0 = ev.size(); for (size_t k = 0; k < n0; ++k) ev.push_back({ev[k].beat + 8.f, ev[k].len, ev[k].note, ev[k].vel}); }
        break;
      case 3: // ghost choir: slow notes
        bpm = 80; beats = 12;
        ev = {{0, 2.8f, 57, 90}, {3, 2.8f, 55, 90}, {6, 2.8f, 60, 90}, {9, 2.8f, 52, 90}};
        break;
      case 4: // jungle vocal stabs
        ev = {{0, 0.3f, 60, 127}, {1.5f, 0.3f, 60, 100}, {3, 0.3f, 63, 127}, {4, 0.3f, 58, 110},
              {6, 0.3f, 60, 127}, {7.5f, 0.3f, 65, 100}, {9, 0.3f, 63, 127}, {10, 0.3f, 58, 110},
              {12, 0.3f, 60, 127}, {12.75f, 0.3f, 60, 90}, {13.5f, 0.3f, 67, 127}};
        break;
      case 5: // drone
        bpm = 60; beats = 10;
        ev = {{0, 9.5f, 36, 100}};
        break;
      case 6: // hollow sub
        ev = {{0, 1.8f, 28, 110}, {2, 1.8f, 31, 110}, {4, 1.8f, 26, 110}, {6, 1.8f, 28, 110},
              {8, 3.8f, 23, 110}, {12, 3.5f, 28, 110}};
        break;
      case 7: // lead
        ev = {{0, 0.9f, 64, 120}, {1, 0.4f, 67, 100}, {1.5f, 1.4f, 69, 120}, {3, 0.9f, 72, 110},
              {4, 0.4f, 71, 100}, {4.5f, 0.4f, 69, 100}, {5, 2.8f, 67, 127}, {8, 0.9f, 64, 120},
              {9, 0.4f, 62, 100}, {9.5f, 2.4f, 64, 127}, {12, 3.5f, 57, 127}};
        break;
    }
    auto x = renderRiff(pt, ev, bpm, beats, 0.003f, i == 3 || i == 5 ? 0.4f : 0.06f, sweepA, sweepB);
    // Ghost/drone get a slow fade-in to sound like pads.
    writeWav(dir + "/" + pt.file + ".wav", x);
    const float pk = peakOf(x);
    size_t clips = 0; for (float s : x) if (std::fabs(s) >= 0.99f) ++clips;
    REPORT("%-26s %8.3f %8.1f %8.1f %9.3f%%\n", pt.name, pk, db(pk), db(rmsOf(x)), 100.0 * clips / x.size());
    CHECK(allFinite(x), "%s produced non-finite samples", pt.name);
    CHECK(pk <= 1.0f, "%s exceeds full scale", pt.name);
    CHECK(rmsOf(x) > 0.02f, "%s is too quiet", pt.name);
  }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
static void testHelpers() {
  REPORT("\n== Math helper accuracy (vs libm) ==\n");
  double e1 = 0, e2 = 0, e3 = 0, e4 = 0;
  for (float x = -8.f; x <= 8.f; x += 0.001f) e1 = std::max(e1, std::fabs(fmut::exp2_fast(x) / exp2f(x) - 1.0));
  for (float x = 0.001f; x < 30000.f; x *= 1.003f) e2 = std::max(e2, std::fabs((double)fmut::log2_fast(x) - log2(x)));
  for (float f = 0.0001f; f <= 0.23f; f += 0.0001f) e3 = std::max(e3, std::fabs(fmut::tanpi_fast(f) / tan(M_PI * f) - 1.0));
  for (float x = 0.05f; x <= 4.f; x += 0.001f) e4 = std::max(e4, std::fabs(fmut::rsqrt_fast(x) * sqrt(x) - 1.0));
  REPORT("exp2_fast   max rel err %.2e\nlog2_fast   max abs err %.2e\ntanpi_fast  max rel err %.2e (f<=0.23)\nrsqrt_fast  max rel err %.2e\n", e1, e2, e3, e4);
  CHECK(e1 < 1e-4, "exp2_fast inaccurate");
  CHECK(e2 < 1e-2, "log2_fast inaccurate");
  CHECK(e3 < 2e-3, "tanpi_fast inaccurate");
  CHECK(e4 < 1e-5, "rsqrt_fast inaccurate");
}

static void testPitch() {
  REPORT("\n== Pitch accuracy (DETUNE 0, BRIGHT 45 = saw, RESO 20, GROWL 0, DRIVE 0, MOD 0) ==\n");
  REPORT("METAL is inharmonic by design (ring/PM at non-integer ratio) and is excluded.\n");
  const int notes[] = {28, 40, 52, 64, 76, 88};
  const char *mn[] = {"TALK", "GRWL", "METL", "GHST"};
  for (int mode : {0, 1, 3}) {
    double worst = 0;
    for (int note : notes) {
      Patch pt = {"pitch", "", {512, 0, mode, 0, 20, 0, 45, 35, 0, 0}};
      Driver d; d.apply(pt); d.note = (float)note; d.e.noteOn(note, 100);
      std::vector<float> x(kFs * 3);
      d.render(x.data(), (uint32_t)x.size());
      const double expect = 440.0 * pow(2.0, (note - 69) / 12.0);
      const double f = estimateF0(x, expect);
      const double cents = 1200.0 * log2(f / expect);
      worst = std::max(worst, std::fabs(cents));
      REPORT("  %s note %3d  expect %9.3f Hz  measured %9.3f Hz  %+6.2f cents\n", mn[mode], note, expect, f, cents);
    }
    // With DETUNE 0 the two oscillators are spread by 0.5 cent by design
    // (osc1 -0.125 c, osc2 +0.5 c), so allow 1 cent.
    CHECK(worst < 1.0, "%s pitch error %.2f cents", mn[mode], worst);
  }
}

static void testVowels() {
  REPORT("\n== Vowel identity: measured formant envelope vs table (TALK, A1 = 55 Hz saw, RESO 50) ==\n");
  REPORT("Envelope = harmonic amplitude x harmonic number (removes the saw's 1/n tilt).\n");
  const char *vn[] = {"A", "E", "I", "O", "U"};
  const int pos[] = {0, 256, 512, 767, 1023};
  const double F1[] = {600, 400, 250, 400, 350};
  const double F2[] = {1040, 1620, 1750, 750, 600};
  const double f0 = 55.0;
  for (int v = 0; v < 5; ++v) {
    Patch pt = {"v", "", {pos[v], 0, 0, 0, 50, 0, 45, 35, 0, 0}};
    Driver d; d.apply(pt); d.note = 33; d.e.noteOn(33, 100);
    std::vector<float> x(kFs * 3);
    d.render(x.data(), (uint32_t)x.size());
    const size_t N = 1 << 17;
    auto m = spectrum(x, x.size() - N, N);
    const double binHz = (double)kFs / (double)N;
    std::vector<double> env(64, 0.0);
    for (int h = 1; h < 64; ++h) {
      const size_t b = (size_t)llround(h * f0 / binHz);
      double pk = 0; for (size_t k = b - 4; k <= b + 4; ++k) pk = std::max(pk, m[k]);
      env[h] = pk * h;
    }
    // F1: strongest harmonic 150..1000 Hz; F2: strongest local max above F1*1.3 up to 2200 Hz
    int h1 = 0; for (int h = 3; h * f0 <= 1000; ++h) if (!h1 || env[h] > env[h1]) h1 = h;
    int h2 = 0;
    for (int h = h1 + 2; h * f0 <= 2200 && h < 63; ++h) {
      if (h * f0 < F1[v] * 1.3 && h * f0 < h1 * f0 * 1.3) continue;
      if (env[h] >= env[h - 1] && env[h] >= env[h + 1] && (!h2 || env[h] > env[h2])) h2 = h;
    }
    const double mf1 = h1 * f0, mf2 = h2 * f0;
    REPORT("  %s  F1 table %4.0f measured %4.0f Hz (%+.2f oct) | F2 table %4.0f measured %4.0f Hz (%+.2f oct)\n",
           vn[v], F1[v], mf1, log2(mf1 / F1[v]), F2[v], mf2, h2 ? log2(mf2 / F2[v]) : 0.0);
    CHECK(std::fabs(log2(mf1 / F1[v])) < 0.3, "vowel %s F1 off", vn[v]);
    CHECK(h2 && std::fabs(log2(mf2 / F2[v])) < 0.3, "vowel %s F2 off", vn[v]);
  }
}

static void testVowelSweepClicks() {
  REPORT("\n== Vowel sweep smoothness (knob A 0->1023 in 10-bit steps, 1 s) ==\n");
  REPORT("Click metric = peak |2nd difference| / RMS. Sweep should stay close to the static worst case.\n");
  const char *mn[] = {"TALK", "GRWL", "METL", "GHST"};
  for (int mode = 0; mode < 4; ++mode) {
    float staticWorst = 0;
    for (int v : {0, 256, 512, 767, 1023}) {
      Patch pt = {"s", "", {v, 300, mode, 0, 70, 20, 50, 35, 0, 30}};
      Driver d; d.apply(pt); d.note = 33; d.e.noteOn(33, 100);
      std::vector<float> x(kFs);
      d.render(x.data(), (uint32_t)x.size());
      staticWorst = std::max(staticWorst, clickMetric(x, kFs / 4, x.size()));
    }
    Patch pt = {"s", "", {0, 300, mode, 0, 70, 20, 50, 35, 0, 30}};
    Driver d; d.apply(pt); d.note = 33; d.e.noteOn(33, 100);
    std::vector<float> x(kFs * 3 / 2);
    // 0.25 s settle, then sweep across 1 s in 64-frame callbacks (like hardware)
    for (size_t pos = 0; pos < x.size(); pos += 64) {
      if (pos >= (size_t)kFs / 4) {
        const float u = std::min(1.f, (float)(pos - kFs / 4) / (float)kFs);
        d.e.setParam(FM_P_VOWEL, (int32_t)lrintf(u * 1023.f));
      }
      d.render(&x[pos], 64);
    }
    const float sweep = clickMetric(x, kFs / 4, x.size());
    REPORT("  %s  static worst %.3f   sweep %.3f   ratio %.2f\n", mn[mode], staticWorst, sweep, sweep / staticWorst);
    CHECK(sweep < staticWorst * 1.5f, "%s vowel sweep shows discontinuities", mn[mode]);
  }
}

static void testNoteEnvelope() {
  REPORT("\n== Note-on behaviour: formant envelope (MOD DPTH -80 vs 0, RATE 50 = 150 ms decay) ==\n");
  auto run = [&](int depth) {
    Patch pt = {"e", "", {100, 0, 0, 0, 60, 10, 50, 50, depth, 0}};
    Driver d; d.apply(pt); d.note = 48;
    std::vector<float> x(kFs);
    d.render(x.data(), 4800);              // 0.1 s before the note
    d.e.noteOn(48, 127);
    d.render(x.data() + 4800, (uint32_t)x.size() - 4800);
    d.e.noteOff(48); d.e.allNoteOff(); d.e.noteOff(48);   // extra offs must be harmless
    std::vector<float> y(4800); d.render(y.data(), 4800);
    CHECK(allFinite(y), "output after note-off not finite");
    return x;
  };
  auto centroid = [&](const std::vector<float> &x, size_t start) {
    const size_t N = 1024; auto m = spectrum(x, start, N);
    double a = 0, b = 0; for (size_t i = 1; i < m.size(); ++i) { a += i * m[i]; b += m[i]; }
    return a / b * kFs / N;
  };
  auto env = run(-80), ref = run(0);
  const size_t early = 4800 + 240, late = kFs - 1024;
  const double e0 = centroid(env, early) / centroid(ref, early);
  const double e1 = centroid(env, late) / centroid(ref, late);
  REPORT("  centroid ratio env/ref: 5-26 ms after note-on %.2f, 0.98 s after note-on %.2f\n", e0, e1);
  CHECK(e0 > 1.2, "envelope did not raise formants at note-on");
  CHECK(std::fabs(e1 - 1.0) < 0.05, "envelope did not decay back");
}

static void testExtremes() {
  REPORT("\n== Extreme settings sweep (all modes, min/max of every edit param, notes 0..127) ==\n");
  size_t combos = 0, bad = 0; float worstPeak = 0, worstDc = 0, quietest = 1e9;
  char quietInfo[96] = "", dcInfo[96] = "";
  const int32_t vals[][2] = {{0, 1023}, {0, 1023}, {0, 3}, {-24, 24}, {0, 100}, {0, 100}, {0, 100}, {0, 100}, {-100, 100}, {0, 100}};
  for (int mode = 0; mode < 4; ++mode)
    for (int mask = 0; mask < 256; mask += 1) {           // 8 bits over params 1,3..9
      if ((mask * 2654435761u) % 7) continue;              // deterministic subsample (~37 per mode)
      for (int note = 0; note <= 127; note += 21) {
        Driver d;
        int bit = 0;
        for (int p = 0; p < FM_NUM_PARAMS; ++p) {
          if (p == FM_P_MODE) { d.e.setParam(p, mode); continue; }
          if (p == FM_P_VOWEL) { d.e.setParam(p, (note * 8) % 1024); continue; }
          d.e.setParam(p, vals[p][(mask >> (bit++ % 8)) & 1]);
        }
        d.e.setParam(FM_P_RESO, 100); d.e.setParam(FM_P_DRIVE, 100);
        d.note = (float)note; d.e.noteOn(note, 127);
        std::vector<float> x(kFs / 2);
        d.hwLfo = 1.f;
        d.render(x.data(), (uint32_t)x.size());
        ++combos;
        if (!allFinite(x)) { ++bad; continue; }
        worstPeak = std::max(worstPeak, peakOf(x));
        if (note >= 21 && note <= 108) {   // piano range; sub-audio notes have no meaningful DC
          // mean over an integer number of fundamental periods
          const double per = kFs / (440.0 * pow(2.0, (note - 69) / 12.0));
          const size_t len = (size_t)llround(floor((x.size() - kFs / 4) / per) * per);
          const float dcv = std::fabs(meanOf(x, x.size() - len, x.size()));
          if (dcv > worstDc) { worstDc = dcv; snprintf(dcInfo, sizeof dcInfo, "mode %d note %d mask %d", mode, note, mask); }
          const float r = rmsOf(x, kFs / 4, x.size());
          if (r < quietest) { quietest = r; snprintf(quietInfo, sizeof quietInfo, "mode %d note %d mask %d", mode, note, mask); }
        }
      }
    }
  REPORT("  %zu renders, %zu non-finite, worst peak %.3f, worst DC %.4f [%s], quietest RMS (notes 21..108) %.1f dBFS [%s]\n",
         combos, bad, worstPeak, worstDc, dcInfo, db(quietest), quietInfo);
  CHECK(bad == 0, "non-finite output at extreme settings");
  CHECK(worstPeak <= 1.f, "exceeded full scale");
  CHECK(worstDc < 0.05f, "DC offset too large");
}

static void testSoak(double seconds) {
  REPORT("\n== Soak test: %.0f s of audio, random params every callback, random notes, random block sizes 1..256 ==\n", seconds);
  Driver d; uint32_t r = 987654321u;
  auto rnd = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; };
  const size_t total = (size_t)(seconds * kFs);
  std::vector<float> buf(256);
  size_t done = 0, nonfinite = 0; float pk = 0; double sumsq = 0;
  const int32_t lo[] = {0, 0, 0, -24, 0, 0, 0, 0, -100, 0}, hi[] = {1023, 1023, 3, 24, 100, 100, 100, 100, 100, 100};
  while (done < total) {
    const uint32_t n = 1 + rnd() % 256;
    if (rnd() % 4 == 0) { const int p = rnd() % FM_NUM_PARAMS; d.e.setParam(p, lo[p] + (int32_t)(rnd() % (uint32_t)(hi[p] - lo[p] + 1))); }
    if (rnd() % 40 == 0) { d.note = (float)(rnd() % 128) + (float)(rnd() % 256) / 256.f; d.e.noteOn((uint8_t)d.note, rnd() % 128); }
    if (rnd() % 60 == 0) d.e.noteOff(0);
    if (rnd() % 500 == 0) d.e.allNoteOff();
    if (rnd() % 3000 == 0) d.e.reset();
    d.hwLfo = ((float)(rnd() % 2001) - 1000.f) / 1000.f;
    d.block = n;
    d.render(buf.data(), n);
    for (uint32_t i = 0; i < n; ++i) {
      if (!std::isfinite(buf[i])) ++nonfinite;
      else { pk = std::max(pk, std::fabs(buf[i])); sumsq += (double)buf[i] * buf[i]; }
    }
    done += n;
  }
  REPORT("  %zu samples, %zu non-finite, peak %.3f, RMS %.1f dBFS\n", done, nonfinite, pk, db((float)sqrt(sumsq / done)));
  CHECK(nonfinite == 0, "soak produced non-finite samples");
  CHECK(pk <= 1.f, "soak exceeded full scale");
}

static void testHostSpeed() {
  REPORT("\n== Host speed (informational only - NOT a Cortex-M7 benchmark) ==\n");
  const char *mn[] = {"TALK", "GRWL", "METL", "GHST"};
  for (int mode = 0; mode < 4; ++mode) {
    Patch pt = {"c", "", {512, 700, mode, 0, 80, 40, 60, 60, 60, 60}};
    Driver d; d.apply(pt); d.note = 40; d.e.noteOn(40, 100);
    std::vector<float> x(kFs * 10);
    auto t0 = std::chrono::high_resolution_clock::now();
    d.render(x.data(), (uint32_t)x.size());
    auto t1 = std::chrono::high_resolution_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / x.size();
    REPORT("  %s  %.1f ns/sample on this host (%.2f%% of real time)\n", mn[mode], ns, ns / (1e9 / kFs) * 100.0);
  }
}

static void renderDiagnostics(const std::string &dir) {
  // Vowel sweep per mode (for listening).
  const char *mn[] = {"talk", "growl", "metal", "ghost"};
  for (int mode = 0; mode < 4; ++mode) {
    Patch pt = {"sweep", "", {0, 300, mode, 0, 65, 25, 50, 35, 0, 25}};
    Driver d; d.apply(pt); d.note = 36; d.e.noteOn(36, 110);
    std::vector<float> x(kFs * 4);
    for (size_t pos = 0; pos < x.size(); pos += 64) {
      const float u = (float)pos / (float)x.size();
      const float tri = u < 0.5f ? u * 2.f : 2.f - u * 2.f;
      d.e.setParam(FM_P_VOWEL, (int32_t)lrintf(tri * 1023.f));
      d.render(&x[pos], 64);
    }
    writeWav(dir + "/diag_vowel_sweep_" + mn[mode] + ".wav", x);
  }
  // Growl knob sweep in GROWL mode.
  {
    Patch pt = {"g", "", {250, 0, 1, 0, 70, 25, 60, 45, 0, 40}};
    Driver d; d.apply(pt); d.note = 28; d.e.noteOn(28, 110);
    std::vector<float> x(kFs * 4);
    for (size_t pos = 0; pos < x.size(); pos += 64) {
      d.e.setParam(FM_P_GROWL, (int32_t)lrintf((float)pos / x.size() * 1023.f));
      d.render(&x[pos], 64);
    }
    writeWav(dir + "/diag_growl_knob_sweep.wav", x);
  }
  // Brightness sweep.
  {
    Patch pt = {"b", "", {300, 200, 0, 0, 55, 20, 0, 35, 0, 20}};
    Driver d; d.apply(pt); d.note = 40; d.e.noteOn(40, 110);
    std::vector<float> x(kFs * 4);
    for (size_t pos = 0; pos < x.size(); pos += 64) {
      d.e.setParam(FM_P_BRIGHT, (int32_t)lrintf((float)pos / x.size() * 100.f));
      d.render(&x[pos], 64);
    }
    writeWav(dir + "/diag_brightness_sweep.wav", x);
  }
}

int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : "test/out";
  const double soakSec = argc > 2 ? atof(argv[2]) : 600.0;
  g_report = fopen((dir + "/test_report.txt").c_str(), "w");
  REPORT("FORMANT MUTATOR offline test harness\n");
  testHelpers();
  testPitch();
  testVowels();
  testVowelSweepClicks();
  testNoteEnvelope();
  testExtremes();
  testSoak(soakSec);
  testHostSpeed();
  renderPatches(dir);
  renderDiagnostics(dir);
  REPORT("\n%s (%d failure%s)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures, g_failures == 1 ? "" : "s");
  if (g_report) fclose(g_report);
  return g_failures ? 1 : 0;
}
