/*
 *  FMdiag - microKORG 2 diagnostic oscillator for FORMANT MUTATOR.
 *
 *  Uses exactly the same voice gating, pitch handling, buffer offsets and
 *  output lanes as microkorg2/unit.cc, but generates a plain tone, so it
 *  costs almost no CPU:
 *
 *    - pure SINE  -> the synth sends voice events (voice gating works)
 *    - buzzy SAW  -> no voice events seen, so FMutator renders every voice
 *
 *  If FMdiag sounds clean but FMutator crackles, the cause is CPU load.
 *  If FMdiag crackles too, the cause is in the shared output handling.
 */
#include "unit.h"
#include "runtime.h"
#include "unit_osc.h"
#include "utils/io_ops.h"

#include <cstdint>

namespace {
unit_runtime_desc_t s_desc;
float s_phase[kMk2MaxVoices];
bool s_active[kMk2MaxVoices];
bool s_events_seen = false;
int32_t s_tone = 0;

inline bool finite_bits(float x) {
  union { float f; uint32_t u; } v;
  v.f = x;
  return (v.u & 0x7F800000u) != 0x7F800000u;
}
// 2^x for |x| < 20 (same idea as fmut::exp2_fast)
inline float exp2_approx(float x) {
  int32_t xi = (int32_t)x;
  if (x < (float)xi) --xi;
  const float u = x - (float)xi - 0.5f;
  const float p = 1.41421356f * (1.f + u * (0.69314718f + u * (0.24022651f + u * (0.05550411f + u * 0.00961813f))));
  union { float f; int32_t i; } r;
  r.f = p;
  r.i += xi << 23;
  return r.f;
}
inline float sin2pi(float t) {               // parabolic sine, t in [0,1)
  const float y = 2.f * t - 1.f;
  const float ay = y < 0.f ? -y : y;
  const float s0 = 4.f * y - 4.f * y * ay;
  const float as0 = s0 < 0.f ? -s0 : s0;
  return -(0.225f * (s0 * as0 - s0) + s0);
}
}  // namespace

__attribute__((used)) int8_t unit_init(const unit_runtime_desc_t *desc) {
  if (!desc) return k_unit_err_undef;
  if (desc->target != unit_header.target) return k_unit_err_target;
  if (!UNIT_API_IS_COMPAT(desc->api)) return k_unit_err_api_version;
  if (desc->samplerate != 48000) return k_unit_err_samplerate;
  s_desc = *desc;
  s_events_seen = false;
  for (int v = 0; v < kMk2MaxVoices; ++v) { s_phase[v] = 0.f; s_active[v] = false; }
  return k_unit_err_none;
}
__attribute__((used)) void unit_teardown() {}
__attribute__((used)) void unit_reset() {}
__attribute__((used)) void unit_resume() {}
__attribute__((used)) void unit_suspend() {}

__attribute__((used)) void unit_render(const float *in, float *out, uint32_t frames) {
  (void)in;
  const unit_runtime_osc_context_t *c = static_cast<const unit_runtime_osc_context_t *>(s_desc.hooks.runtime_context);
  const int voices = (int)c->voiceLimit <= (int)kMk2MaxVoices ? (int)c->voiceLimit : (int)kMk2MaxVoices;
  for (int v = 0; v < voices; ++v) {
    const uint32_t lane = (voices >= 4) ? (uint32_t)(v & 3) : (uint32_t)(c->voiceOffset + v);
    const uint32_t offset = GetBufferOffset(c, v & ~3, frames);
    const bool live = !s_events_seen || s_active[v];
    float note = c->pitch[v];
    if (!finite_bits(note) || note < 0.f || note > 140.f) note = 60.f;
    float w = 440.f * exp2_approx((note - 69.f) * (1.f / 12.f)) * (1.f / 48000.f);
    if (w > 0.45f) w = 0.45f;
    float ph = s_phase[v];
    for (uint32_t i = 0; i < frames; ++i) {
      float s = 0.f;
      if (live) s = s_events_seen ? 0.25f * sin2pi(ph) : 0.15f * (2.f * ph - 1.f);
      write_oscillator_output_x1(out, s, offset, c->outputStride, i, lane);
      ph += w; if (ph >= 1.f) ph -= 1.f;
    }
    s_phase[v] = ph;
  }
}

__attribute__((used)) void unit_set_param_value(uint8_t id, int32_t value) { if (id == 0) s_tone = value; }
__attribute__((used)) int32_t unit_get_param_value(uint8_t id) { return id == 0 ? s_tone : 0; }
__attribute__((used)) const char *unit_get_param_str_value(uint8_t id, int32_t value) {
  (void)value;
  if (id == 0) return s_events_seen ? "EVENTS" : "NO EVT";
  return nullptr;
}
__attribute__((used)) const uint8_t *unit_get_param_bmp_value(uint8_t, int32_t) { return nullptr; }
__attribute__((used)) void unit_set_tempo(uint32_t) {}
__attribute__((used)) void unit_platform_exclusive(uint8_t, void *, uint32_t) {}
__attribute__((used)) void unit_osc_voice_event(uint8_t event, uint8_t voice, uint8_t, uint8_t) {
  if (voice >= kMk2MaxVoices) return;
  s_events_seen = true;
  if (event == k_voice_event_allocation || event == k_voice_event_steal) s_active[voice] = true;
  else if (event == k_voice_event_deallocation) s_active[voice] = false;
}
