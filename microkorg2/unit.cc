/*
 *  File: unit.cc
 *
 *  FORMANT MUTATOR - microKORG 2 oscillator unit interface (logue SDK 2.1).
 *
 *  The microKORG 2 runs oscillator units polyphonically: one render call
 *  produces up to 8 voices (ctxt->voiceLimit), each with its own pitch,
 *  written to an interleaved output buffer. Each voice gets its own instance
 *  of the shared DSP engine (formant_mutator/formant_engine.h), the same code
 *  as the NTS-1 mkII build. Callback names, buffer layout, mod-matrix data
 *  access and voice events follow Korg's microkorg2/waves and vox examples.
 */

#include "unit.h"
#include "runtime.h"
#include "unit_osc.h"
#include "utils/io_ops.h"
#include "utils/mk2_utils.h"

#include <cstddef>
#include <cstdint>

#include "fm_params.h"
#include "formant_engine.h"
#include "formant_group4.h"

namespace {

enum { kNumUnitParams = 13 };

// microKORG 2 parameter index -> engine parameter ID
const uint8_t k_param_map[kNumUnitParams] = {
    FM_P_VOWEL, FM_P_GROWL, FM_P_MODE,                              // page 1
    FM_P_SHIFT, FM_P_RESO, FM_P_DETUNE, FM_P_BRIGHT, FM_P_DRIVE,    // page 2
    FM_P_RATE, FM_P_DEPTH, FM_P_ENV_DECAY, FM_P_ENV_AMT, FM_P_SUB,  // page 3
};

const char *const k_mode_names[FM_MODE_MAX + 1] = {"TALK", "GROWL", "METAL", "GHOST"};

// Mod-matrix destinations offered to the microKORG 2 (names max 8 chars).
const char *const k_mod_dest_names[fmut::kNumModDest] = {
    "Vowel", "Growl", "Shift", "Reso", "Bright", "Drive"};

// Per-voice output trim: voices are summed by the synth (Korg's waves uses 0.3).
const float k_voice_trim = 0.4f;

fmut::Tables s_tables;
fmut::Engine s_voice[kMk2MaxVoices];
fmut::Engine s_spare[4];          // fill unused SIMD lanes when fewer than 4 voices
int32_t s_param[kNumUnitParams];
unit_runtime_desc_t s_desc;

// Voice activity, driven by unit_osc_voice_event. Only sounding voices are
// rendered; idle voices output silence and cost (almost) nothing. Until the
// first voice event arrives we render every voice, so nothing goes silent if
// a host never sends events.
bool s_active[kMk2MaxVoices];
bool s_events_seen = false;
fmut::g4f s_out4[fmut::kCtrlBlock];

inline const unit_runtime_osc_context_t *ctx() {
  return static_cast<const unit_runtime_osc_context_t *>(s_desc.hooks.runtime_context);
}

void applyParam(uint8_t index, int32_t value) {
  s_param[index] = value;
  const uint8_t id = k_param_map[index];
  for (int v = 0; v < kMk2MaxVoices; ++v) s_voice[v].setParam(id, value);
  for (int j = 0; j < 4; ++j) s_spare[j].setParam(id, value);
}

}  // namespace

__attribute__((used)) int8_t unit_init(const unit_runtime_desc_t *desc) {
  if (!desc)
    return k_unit_err_undef;
  if (desc->target != unit_header.target)
    return k_unit_err_target;
  if (!UNIT_API_IS_COMPAT(desc->api))
    return k_unit_err_api_version;
  if (desc->samplerate != 48000)
    return k_unit_err_samplerate;

  s_desc = *desc;
  s_tables.build();
  s_events_seen = false;
  for (int v = 0; v < kMk2MaxVoices; ++v) {
    s_active[v] = false;
    s_voice[v].init(&s_tables, 0x9E3779B9u * (uint32_t)(v + 1));
    s_voice[v].setSplitEnv(true);
  }
  for (int j = 0; j < 4; ++j) {
    s_spare[j].init(&s_tables, 0x7F4A7C15u * (uint32_t)(j + 1));
    s_spare[j].setSplitEnv(true);
    s_spare[j].setPitch(220.f / 48000.f);
  }
  for (int i = 0; i < kNumUnitParams; ++i)
    applyParam((uint8_t)i, unit_header.params[i].init);
  return k_unit_err_none;
}

__attribute__((used)) void unit_teardown() {}

__attribute__((used)) void unit_reset() {
  for (int v = 0; v < kMk2MaxVoices; ++v) s_voice[v].reset();
}

__attribute__((used)) void unit_resume() {}

__attribute__((used)) void unit_suspend() {}

__attribute__((used)) void unit_render(const float *in, float *out, uint32_t frames) {
  (void)in;
  const unit_runtime_osc_context_t *c = ctx();
  const int voices = (int)c->voiceLimit <= (int)kMk2MaxVoices ? (int)c->voiceLimit : (int)kMk2MaxVoices;

  // Voices are rendered four at a time in SIMD lanes (as Korg's vox does).
  for (int base = 0; base < voices; base += 4) {
    fmut::Engine *e[4];
    bool live[4];
    bool any = false;
    for (int j = 0; j < 4; ++j) {
      const int v = base + j;
      if (v < voices) {
        e[j] = &s_voice[v];
        live[j] = !s_events_seen || s_active[v];
        any |= live[j];
        if (live[j]) {
          // ctxt->pitch is a fractional MIDI note number per voice.
          float note = c->pitch[v];
          if (!fmut::finite_bits(note) || note < 0.f || note > 140.f) note = 60.f;
          s_voice[v].setPitch(440.f * fmut::exp2_fast((note - 69.f) * (1.f / 12.f)) * (1.f / 48000.f));
        }
      } else {
        e[j] = &s_spare[j];
        live[j] = false;
      }
    }

    const uint32_t offset = GetBufferOffset(c, base, frames);
    // Output lane: with 4 or 8 voices, voice v sits in lane v%4 of its group
    // (Korg's waves/vox); with 1 or 2 voices the lanes start at voiceOffset
    // (Korg's vox x1/x2 paths).
    uint32_t lane[4];
    for (int j = 0; j < 4; ++j) lane[j] = (voices >= 4) ? (uint32_t)j : (uint32_t)(c->voiceOffset + j);
    const int lanesUsed = (voices - base) < 4 ? (voices - base) : 4;

    if (!any) {                                   // whole group idle: silence, no DSP
      for (uint32_t i = 0; i < frames; ++i)
        for (int j = 0; j < lanesUsed; ++j)
          write_oscillator_output_x1(out, 0.f, offset, c->outputStride, i, lane[j]);
      continue;
    }

    uint32_t done = 0;
    while (done < frames) {
      const uint32_t n = (frames - done) < (uint32_t)fmut::kCtrlBlock ? (frames - done) : (uint32_t)fmut::kCtrlBlock;
      fmut::Group4::render(e, s_out4, n);
      for (uint32_t i = 0; i < n; ++i)
        for (int j = 0; j < lanesUsed; ++j)
          write_oscillator_output_x1(out, live[j] ? s_out4[i][j] * k_voice_trim : 0.f,
                                     offset, c->outputStride, done + i, lane[j]);
      done += n;
    }
  }
}

__attribute__((used)) void unit_set_param_value(uint8_t id, int32_t value) {
  if (id >= kNumUnitParams) return;
  const int32_t lo = unit_header.params[id].min, hi = unit_header.params[id].max;
  value = value < lo ? lo : (value > hi ? hi : value);
  applyParam(id, value);
}

__attribute__((used)) int32_t unit_get_param_value(uint8_t id) {
  return id < kNumUnitParams ? s_param[id] : 0;
}

__attribute__((used)) const char *unit_get_param_str_value(uint8_t id, int32_t value) {
  if (id == 2) {  // MODE
    value = value < FM_MODE_MIN ? FM_MODE_MIN : (value > FM_MODE_MAX ? FM_MODE_MAX : value);
    return k_mode_names[value];
  }
  return nullptr;
}

__attribute__((used)) const uint8_t *unit_get_param_bmp_value(uint8_t id, int32_t value) {
  (void)id; (void)value;
  return nullptr;
}

__attribute__((used)) void unit_set_tempo(uint32_t tempo) { (void)tempo; }

__attribute__((used)) void unit_platform_exclusive(uint8_t messageId, void *data, uint32_t dataSize) {
  (void)dataSize;
  const unit_runtime_osc_context_t *c = ctx();
  switch (messageId) {
    case kMk2PlatformExclusiveModData: {
      // Sum every mod-matrix slot routed to one of our destinations, per voice.
      const int32_t *index = GetModIndex(data);
      const float *depth = GetModDepth(data);
      const int voices = (int)c->voiceLimit <= (int)kMk2MaxVoices ? (int)c->voiceLimit : (int)kMk2MaxVoices;
      for (int v = 0; v < voices; ++v) {
        float m[fmut::kNumModDest] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        for (int src = 0; src < kNumMk2ModSrc; ++src) {
          const int32_t dest = index[src];
          if (dest < 0 || dest >= fmut::kNumModDest) continue;
          const float *d = GetModSourceData(data, src, c->voiceLimit, v);
          m[dest] += depth[src] * d[0];
        }
        s_voice[v].setMod(fmut::kModVowel,  m[fmut::kModVowel]);
        s_voice[v].setMod(fmut::kModGrowl,  m[fmut::kModGrowl]);
        s_voice[v].setMod(fmut::kModShift,  m[fmut::kModShift] * 2.f);   // +-2 octaves
        s_voice[v].setMod(fmut::kModReso,   m[fmut::kModReso]);
        s_voice[v].setMod(fmut::kModBright, m[fmut::kModBright]);
        s_voice[v].setMod(fmut::kModDrive,  m[fmut::kModDrive]);
      }
    } break;

    case kMk2PlatformExclusiveModDestName: {
      // data[0] holds the destination index; the name is written from data[1]
      // (same convention as Korg's vox example). Unnamed slots stay empty.
      char *name = GetModDestNameData(data);
      const uint8_t idx = (uint8_t)name[0];
      if (idx < fmut::kNumModDest) {
        const char *src = k_mod_dest_names[idx];
        for (int i = 0; src[i] && i < 8; ++i) name[1 + i] = src[i];
      }
    } break;

    default:
      break;
  }
}

__attribute__((used)) void unit_osc_voice_event(uint8_t event, uint8_t voice, uint8_t note, uint8_t velocity) {
  if (voice >= kMk2MaxVoices) return;
  s_events_seen = true;
  switch (event) {
    case k_voice_event_allocation:
      // Fresh voice: clear filter/oscillator state so it starts cleanly.
      if (!s_active[voice]) s_voice[voice].reset();
      s_active[voice] = true;
      s_voice[voice].noteOn(note, velocity);
      break;
    case k_voice_event_steal:
      s_active[voice] = true;
      s_voice[voice].noteOn(note, velocity);
      break;
    case k_voice_event_release:
      s_voice[voice].noteOff(note);     // keeps rendering through the amp release
      break;
    case k_voice_event_deallocation:
      s_active[voice] = false;
      break;
    default:
      break;
  }
}
