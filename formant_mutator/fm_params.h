/*
 *  File: fm_params.h
 *
 *  FORMANT MUTATOR - parameter IDs, ranges and defaults.
 *
 *  Shared by header.c (SDK unit header), the DSP engine and the offline
 *  test harness so that every build uses identical ranges and defaults.
 *  Plain C so it can be included from header.c.
 */

#ifndef FM_PARAMS_H_
#define FM_PARAMS_H_

/* Parameter indices (order is fixed by the NTS-1 mkII osc unit layout:
 * index 0 = A knob, 1 = B knob, 2..9 = the eight Edit menu slots). */
#define FM_P_VOWEL   0  /* A knob      0..1023  vowel morph A-E-I-O-U      */
#define FM_P_GROWL   1  /* B knob      0..1023  growl intensity (per mode) */
#define FM_P_MODE    2  /* Edit 1      0..3     TALK/GRWL/METL/GHST        */
#define FM_P_SHIFT   3  /* Edit 2    -24..24    formant shift, semitones   */
#define FM_P_RESO    4  /* Edit 3      0..100   formant resonance %        */
#define FM_P_DETUNE  5  /* Edit 4      0..100   osc detune / ratio %       */
#define FM_P_BRIGHT  6  /* Edit 5      0..100   harmonic brightness %      */
#define FM_P_RATE    7  /* Edit 6      0..100   mod rate / env decay %     */
#define FM_P_DEPTH   8  /* Edit 7   -100..100   +LFO depth / -ENV amount % */
#define FM_P_DRIVE   9  /* Edit 8      0..100   drive %                    */
#define FM_NUM_PARAMS 10

/* min, max, default */
#define FM_VOWEL_MIN   0
#define FM_VOWEL_MAX   1023
#define FM_VOWEL_DEF   0

#define FM_GROWL_MIN   0
#define FM_GROWL_MAX   1023
#define FM_GROWL_DEF   256

#define FM_MODE_MIN    0
#define FM_MODE_MAX    3
#define FM_MODE_DEF    0

#define FM_SHIFT_MIN   (-24)
#define FM_SHIFT_MAX   24
#define FM_SHIFT_DEF   0

#define FM_RESO_MIN    0
#define FM_RESO_MAX    100
#define FM_RESO_DEF    50

#define FM_DETUNE_MIN  0
#define FM_DETUNE_MAX  100
#define FM_DETUNE_DEF  20

#define FM_BRIGHT_MIN  0
#define FM_BRIGHT_MAX  100
#define FM_BRIGHT_DEF  50

#define FM_RATE_MIN    0
#define FM_RATE_MAX    100
#define FM_RATE_DEF    35

#define FM_DEPTH_MIN   (-100)
#define FM_DEPTH_MAX   100
#define FM_DEPTH_DEF   0

#define FM_DRIVE_MIN   0
#define FM_DRIVE_MAX   100
#define FM_DRIVE_DEF   20

#endif /* FM_PARAMS_H_ */
