/*
 *  File: unit.cc
 *
 *  FORMANT MUTATOR - NTS-1 mkII oscillator unit interface (logue SDK 2.0).
 *  Callback signatures follow platform/nts-1_mkii/common/unit.h and the
 *  official "waves" reference oscillator.
 */

#include "unit_osc.h"
#include "utils/int_math.h"   // clipminmaxi32()
#include "fm_params.h"
#include "formant_engine.h"

static fmut::Tables s_tables;
static fmut::Engine s_engine;
static int32_t s_cached[UNIT_OSC_MAX_PARAM_COUNT];
static const unit_runtime_osc_context_t *s_context = nullptr;

#define q31_to_f32_c 4.65661287307739e-010f
#define q31_to_f32(q) ((float)(q) * q31_to_f32_c)

static const char *const k_mode_names[FM_MODE_MAX + 1] = {"TALK", "GRWL", "METL", "GHST"};

__unit_callback int8_t unit_init(const unit_runtime_desc_t *desc) {
  if (!desc)
    return k_unit_err_undef;
  if (desc->target != unit_header.target)
    return k_unit_err_target;
  if (!UNIT_API_IS_COMPAT(desc->api))
    return k_unit_err_api_version;
  if (desc->samplerate != 48000)
    return k_unit_err_samplerate;
  if (desc->input_channels != 2 || desc->output_channels != 1)
    return k_unit_err_geometry;

  s_context = static_cast<const unit_runtime_osc_context_t *>(desc->hooks.runtime_context);

  for (int id = 0; id < UNIT_OSC_MAX_PARAM_COUNT; ++id)
    s_cached[id] = static_cast<int32_t>(unit_header.params[id].init);

  s_tables.build();          // sine + vowel gain tables; no allocation
  s_engine.init(&s_tables);
  return k_unit_err_none;
}

__unit_callback void unit_teardown() {}

__unit_callback void unit_reset() { s_engine.reset(); }

__unit_callback void unit_resume() {}

__unit_callback void unit_suspend() {}

__unit_callback void unit_render(const float *in, float *out, uint32_t frames) {
  (void)in;  // audio input is not used (runtime assumes unused by default)
  s_engine.setPitch(osc_w0f_for_note((s_context->pitch) >> 8, s_context->pitch & 0xFF));
  s_engine.setShapeLfo(q31_to_f32(s_context->shape_lfo));
  s_engine.process(out, frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value) {
  if (id >= FM_NUM_PARAMS) return;
  value = clipminmaxi32(unit_header.params[id].min, value, unit_header.params[id].max);
  s_cached[id] = value;
  s_engine.setParam(id, value);
}

__unit_callback int32_t unit_get_param_value(uint8_t id) {
  if (id >= FM_NUM_PARAMS) return 0;
  return s_cached[id];
}

__unit_callback const char *unit_get_param_str_value(uint8_t id, int32_t value) {
  if (id == FM_P_MODE) {
    value = clipminmaxi32(FM_MODE_MIN, value, FM_MODE_MAX);
    return k_mode_names[value];
  }
  return nullptr;
}

__unit_callback void unit_note_on(uint8_t note, uint8_t velo) { s_engine.noteOn(note, velo); }

__unit_callback void unit_note_off(uint8_t note) { s_engine.noteOff(note); }

__unit_callback void unit_all_note_off() { s_engine.allNoteOff(); }

__unit_callback void unit_set_tempo(uint32_t tempo) { (void)tempo; }

__unit_callback void unit_tempo_4ppqn_tick(uint32_t counter) { (void)counter; }

__unit_callback void unit_pitch_bend(uint16_t bend) { (void)bend; }  // pitch is read from context->pitch in unit_render, as in Korg's waves example

__unit_callback void unit_channel_pressure(uint8_t press) { (void)press; }

__unit_callback void unit_aftertouch(uint8_t note, uint8_t press) { (void)note; (void)press; }
