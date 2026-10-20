/*
 *  File: header.c
 *
 *  FORMANT MUTATOR - microKORG 2 oscillator unit header (logue SDK 2.1).
 *  The microKORG 2 shows 13 parameters on three pages (3 + 5 + 5),
 *  following the layout of Korg's microkorg2/vox and waves examples.
 *  Names are limited to 8 characters on this platform.
 */

#include "unit.h"
#include "runtime.h"
#include "fm_params.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x5477476EU,           // 'TwGn' - same developer ID as the NTS-1 mkII build
    .unit_id = 0x00000002U,
    .version = 0x00010101U,          // 1.1.1
    .name = "FMutator",              // max 8 characters on microKORG 2
    .num_presets = 0,
    .num_params = 13,
    .params = {
        // min, max, center, default, type, frac, frac mode, reserved, name

        // Page 1
        {FM_VOWEL_MIN,  FM_VOWEL_MAX,  0, FM_VOWEL_DEF,  k_unit_param_type_none,    0, 0, 0, {"VOWEL"}},
        {FM_GROWL_MIN,  FM_GROWL_MAX,  0, FM_GROWL_DEF,  k_unit_param_type_none,    0, 0, 0, {"GROWL"}},
        {FM_MODE_MIN,   FM_MODE_MAX,   0, FM_MODE_DEF,   k_unit_param_type_strings, 0, 0, 0, {"MODE"}},

        // Page 2
        {FM_SHIFT_MIN,  FM_SHIFT_MAX,  0, FM_SHIFT_DEF,  k_unit_param_type_none,    0, 0, 0, {"SHIFT"}},
        {FM_RESO_MIN,   FM_RESO_MAX,   0, FM_RESO_DEF,   k_unit_param_type_percent, 0, 0, 0, {"RESO"}},
        {FM_DETUNE_MIN, FM_DETUNE_MAX, 0, FM_DETUNE_DEF, k_unit_param_type_percent, 0, 0, 0, {"DETUNE"}},
        {FM_BRIGHT_MIN, FM_BRIGHT_MAX, 0, FM_BRIGHT_DEF, k_unit_param_type_percent, 0, 0, 0, {"BRIGHT"}},
        {FM_DRIVE_MIN,  FM_DRIVE_MAX,  0, FM_DRIVE_DEF,  k_unit_param_type_percent, 0, 0, 0, {"DRIVE"}},

        // Page 3
        {FM_RATE_MIN,   FM_RATE_MAX,   0, FM_RATE_DEF,   k_unit_param_type_percent, 0, 0, 0, {"LFO RATE"}},
        {FM_DEPTH_MIN,  FM_DEPTH_MAX,  0, FM_DEPTH_DEF,  k_unit_param_type_percent, 0, 0, 0, {"LFO DPTH"}},
        {FM_ENVDEC_MIN, FM_ENVDEC_MAX, 0, FM_ENVDEC_DEF, k_unit_param_type_percent, 0, 0, 0, {"ENV DCY"}},
        {FM_ENVAMT_MIN, FM_ENVAMT_MAX, 0, FM_ENVAMT_DEF, k_unit_param_type_percent, 0, 0, 0, {"ENV DPTH"}},
        {FM_SUB_MIN,    FM_SUB_MAX,    0, FM_SUB_DEF,    k_unit_param_type_percent, 0, 0, 0, {"SUB"}},
    }};
