/*
 *  FMdiag - microKORG 2 diagnostic oscillator for FORMANT MUTATOR.
 *  Same voice/output handling as FMutator, but a trivial tone generator.
 */
#include "unit.h"
#include "runtime.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x5477476EU,
    .unit_id = 0x00000003U,
    .version = 0x00010200U,
    .name = "FMdiag",
    .num_presets = 0,
    .num_params = 1,
    .params = {
        {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"TONE"}},
    }};
