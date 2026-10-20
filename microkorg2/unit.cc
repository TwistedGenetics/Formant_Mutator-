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
int32_t s_param[kNumUnitParams];
unit_runtime_desc_t s_desc;
float s_tmp[kMk2BufferSize];

inline const unit_runtime_osc_context_t *ctx() {
  return static_cast<const unit_runtime_osc_context_t *>(s_desc.hooks.runtime_context);
}

void applyParam(uint8_t index, int32_t value) {
  s_param[index] = value;
  const uint8_t id = k_param_map[index];
  for (int v = 0; v < kMk2MaxVoices; ++v) s_voice[v].setParam(id, value);
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
  for (int v = 0; v < kMk2MaxVoices; ++v) {
    s_voice[v].init(&s_tables, 0x9E3779B9u * (uint32_t)(v + 1));
    s_voice[v].setSplitEnv(true);
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

  for (int v = 0; v < voices; ++v) {
    // ctxt->pitch is a fractional MIDI note number per voice.
    const float hz = 440.f * fmut::exp2_fast((c->pitch[v] - 69.f) * (1.f / 12.f));
    s_voice[v].setPitch(hz * (1.f / 48000.f));

    const uint32_t offset = GetBufferOffset(c, v, frames);
    uint32_t done = 0;
    while (done < frames) {
      const uint32_t n = (frames - done) < (uint32_t)kMk2BufferSize ? (frames - done) : (uint32_t)kMk2BufferSize;
      s_voice[v].process(s_tmp, n);
      for (uint32_t i = 0; i < n; ++i)
        write_oscillator_output_x1(out, s_tmp[i] * k_voice_trim, offset, c->outputStride, done + i, v);
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
  switch (event) {
    case k_voice_event_allocation:
    case k_voice_event_steal:
      s_voice[voice].noteOn(note, velocity);
      break;
    case k_voice_event_release:
      s_voice[voice].noteOff(note);
      break;
    default:
      break;
  }
}
