/*
 *  File: header.c
 *
 *  FORMANT MUTATOR - NTS-1 mkII oscillator unit header definition
 *  (logue SDK 2.0 unit format, see platform/nts-1_mkii/common/runtime.h)
 */

#include "unit_osc.h"
#include "fm_params.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    // Developer ID 'TwGn'. Not registered in Korg's developer_ids.md (that
    // list is currently only enforced for drumlogue). Keep this identical
    // across all your units; unit_id must be unique per unit.
    .dev_id = 0x5477476EU,
    .unit_id = 0x00000002U,
    .version = 0x00010300U,          // 1.3.0
    .name = "FORMANT MUTATOR",
    .num_params = FM_NUM_PARAMS,
    .params = {
        // min, max, center, default, type, frac, frac mode, reserved, name

        // A knob: vowel morph A-E-I-O-U
        {FM_VOWEL_MIN, FM_VOWEL_MAX, 0, FM_VOWEL_DEF, k_unit_param_type_none, 0, 0, 0, {"VOWEL"}},
        // B knob: growl intensity
        {FM_GROWL_MIN, FM_GROWL_MAX, 0, FM_GROWL_DEF, k_unit_param_type_none, 0, 0, 0, {"GROWL"}},

        // Edit 1..8
        {FM_MODE_MIN,   FM_MODE_MAX,   0, FM_MODE_DEF,   k_unit_param_type_strings, 0, 0, 0, {"MODE"}},
        {FM_SHIFT_MIN,  FM_SHIFT_MAX,  0, FM_SHIFT_DEF,  k_unit_param_type_semi,    0, 0, 0, {"SHIFT"}},
        {FM_RESO_MIN,   FM_RESO_MAX,   0, FM_RESO_DEF,   k_unit_param_type_percent, 0, 0, 0, {"RESO"}},
        {FM_DETUNE_MIN, FM_DETUNE_MAX, 0, FM_DETUNE_DEF, k_unit_param_type_percent, 0, 0, 0, {"DETUNE"}},
        {FM_BRIGHT_MIN, FM_BRIGHT_MAX, 0, FM_BRIGHT_DEF, k_unit_param_type_percent, 0, 0, 0, {"BRIGHT"}},
        {FM_RATE_MIN,   FM_RATE_MAX,   0, FM_RATE_DEF,   k_unit_param_type_percent, 0, 0, 0, {"MOD RATE"}},
        {FM_DEPTH_MIN,  FM_DEPTH_MAX,  0, FM_DEPTH_DEF,  k_unit_param_type_percent, 0, 0, 0, {"MOD DPTH"}},
        {FM_DRIVE_MIN,  FM_DRIVE_MAX,  0, FM_DRIVE_DEF,  k_unit_param_type_percent, 0, 0, 0, {"DRIVE"}}},
};
