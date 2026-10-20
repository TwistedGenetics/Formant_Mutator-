# FORMANT MUTATOR — Optimisation Report

Nothing here is a hardware benchmark. Sizes come from the actual ARM build (`arm-none-eabi-size`, `nm`, `objdump`) of the released unit. CPU figures are static analysis or host timings and are labelled as such.

## Memory (measured from the ARM ELF)

| Item | Bytes | Limit (Korg NTS-1 mkII README) |
|---|---|---|
| `.text` (code + const data) | 11,987 | — |
| `.data` | 228 | — |
| `.bss` (all runtime state) | 2,592 | — |
| Unit file `formant_mutator.nts1mkiiunit` | 13,468 | osc: ~48 KB unit size, 48 KB RAM load |
| Engine object (`s_engine`) | 2,544 | — |
| of which: sine table (513 floats) | 2,052 | — |

- The only external symbol imported from the runtime is `midi_to_hz_lut_f`, through the SDK's `osc_w0f_for_note()`. Korg's `waves` example imports the same symbol (plus several more).
- **No libm**: the earlier draft pulled in `powf`, `expf` and `sinf` (~5 KB of code, plus soft-float doubles). Inline approximations replaced them; the harness checks each one against libm.
- **No heap use anywhere**: there is no `new` or `malloc`, and all state is a static object.
- The NTS-1 mkII oscillator API offers no SDRAM (`getBufferSize()` is 0 in Korg's example). None is needed.

## Processing structure

| Rate | Work |
|---|---|
| Parameter change (outside render) | Range scaling, a few `exp2_fast` calls |
| Control rate, every 16 samples (3 kHz) | Smoothing, LFO, envelope, vowel interpolation of 4×(freq, bw, gain), formant shift `exp2_fast`, 4× `tanpi_fast`, 4× `rsqrt_fast`, per-mode setup, denormal flush |
| Audio rate (48 kHz) | 2 oscillators (morph + polyBLEP), sine sub, mode source path, 4 TPT SVF band-passes, shaper, DC blocker, LPF, safety clip |

### Per-sample cost (static analysis of the ARM build)
- `renderBlock()` compiles to 3,296 bytes of Thumb-2 code, with 673 VFP instructions in total across all mode branches. Only one mode's branch runs per sample.
- **4 floating-point divides per sample**, one per SVF: `a1 = 1/(1 + g(g+k))`. These are needed because the audio-rate formant FM changes `g` every sample. If GROWL is 0 in TALK, GROWL and METAL modes, the FM factor is 1. The divide could then move to control rate (see "Further savings").
- The polyBLEP reciprocals `1/dt` are computed once per block, not per sample.
- No `sin`, `exp`, `pow`, `tan` or `log` calls in the sample loop: the sine comes from a 512-point interpolated table, and tanh is a rational approximation.
- **Host timing (x86-64, `-O2`, informational only):** roughly 28–39 ns per sample across the four modes. This says nothing reliable about the M7, and the per-mode ordering varies from run to run.

### What still needs measuring on hardware
The NTS-1 mkII's clock and the share of each audio block left for a user oscillator are not documented in the SDK. CPU headroom therefore has to be checked on the device (see TEST_PLAN step 9). If you hear dropouts:
1. Set `kNumForm` to 3 in `formant_engine.h`, which drops F4. The vowel tables keep their first three columns.
2. Move the SVF `a1` divide to control rate and drop the audio-rate formant FM (or apply it to `g` only at control rate).
3. Increase `kCtrlBlock` from 16 to 32.

## DSP design decisions

| Decision | Why |
|---|---|
| **TPT / Zavalishin SVF** for formants | Unconditionally stable for any positive g and k, even when coefficients change every sample (audio-rate formant FM). A direct-form biquad is not safe under per-sample modulation at high Q. |
| **Parallel bank, normalised band-pass** (`k·v1`, unity peak) | Gains are independent of Q. Each vowel's per-formant gains (compressed table dB values) give the spectral tilt. |
| **Constant-Q bandwidths + pitch-aware floor** | Shifting formants keeps the character. The floor (≈1.3× f0 at RESO 0, shrinking with RESO) stops formants falling between harmonics on bass notes. The offline vowel test found exactly that problem before the floor was added. |
| **4 formants (F1–F4)** | F4 fitted in the budget, so it is included. F5 was left out: it adds little audible vowel identity at synth pitches and would cost another per-sample divide. |
| **Vowel slew per mode** | Fast in METAL (0.1/block), slow glide in GHOST (0.004/block ≈ 80 ms). |
| **Control-rate coefficients + per-sample linear ramps** | Cheap, and the vowel-sweep click test shows no extra discontinuity over static vowels (ratio ≤ 1.0). |
| **polyBLEP saw and pulse** | Cheap anti-aliasing; good enough because the formant bank and post LPF attenuate the top octave. |
| **No oversampling** | Not justified without hardware CPU measurements (spec rule). Aliasing from the shapers is reduced by bounded shapers (tanh/rational), a ~16 kHz one-pole post-LPF and output compensation. Wavefolding in METAL is the most alias-prone path; it is a single sine fold, blended in by DRIVE. |
| **Feedback growl with loop gain < 1** | Bounded by tanh and a low-pass, with a coefficient chosen so the loop cannot self-oscillate. An earlier, hotter value produced a constant howl at the top of the GROWL knob, which showed up in the spectrogram review. |
| **Denormal protection** | All recursive states (4 SVFs × 2, feedback LP, DC blocker, post-LPF) are flushed below 1e-15 once per control block. The M7 FPU's flush-to-zero setting is not assumed. |
| **Safety soft clip** | The final `tanh_fast` guarantees |out| < 1. Worst peak over the soak and extreme tests was 0.92. |

## Accuracy of the approximations (from `test/out/test_report.txt`)

| Function | Max error | Used at |
|---|---|---|
| `exp2_fast` | 3.3e-6 relative | Control/param rate |
| `log2_fast` | 4.9e-3 absolute (~6 cents) | METAL key-tracking only |
| `tanpi_fast` (f ≤ 0.23) | 1.2e-4 relative | Formant `g` coefficients |
| `rsqrt_fast` | 4.7e-6 relative | Gain makeup |

## Disabled or simplified features

| Spec item | Status |
|---|---|
| 5th formant | Not implemented (see above) |
| Oscillator hard sync | Not implemented. Ring mod and phase mod (METAL) are implemented instead, as the spec allows ("sync or ring modulation"). |
| Oversampling | Not implemented (no hardware measurement to justify it) |
| Tempo-synced LFO | Not implemented. Use the NTS-1 mkII LFO, which modulates VOWEL. |
| Separate sub-osc level control | No spare parameter slot (all 10 are used). The sub level is fixed per mode. |
| Note-off behaviour | The amp EG is the NTS-1 mkII's own. The oscillator keeps running, which is normal for logue oscillators, and keeps a gate count for future use. |

## Further savings if needed
- Make the SVF divide control-rate when GROWL = 0 (TALK/GRWL/METL) or always in GHOST, where there is no formant FM. That saves 4 divides per sample.
- Precompute the vowel tables' gain/Q combinations per vowel pair.
- Reduce the sine table to 256 points, saving 1 KB of RAM.

## microKORG 2 version

| Item | Value |
|---|---|
| Unit file `FormantMutator.mk2unit` | 18,232 bytes (limit ~48 KB unit size / 48 KB RAM load per Korg's README) |
| Shared tables (sine + vowel gains) | ~2.1 KB, one copy for all voices |
| Per-voice engine state | ~0.5 KB × 8 voices |
| SDRAM / external memory | none used (8 KB available to oscillators) |

- **CPU scales with voices.** Each voice runs the full engine (2 oscillators, 4 SVFs with 4 per-sample divides, shaper) on the Cortex-A7. Korg doesn't document the oscillator's CPU budget, so the 8-voice worst case **must be checked on the hardware** (TEST_PLAN checklist D6).
- **Scalar, not NEON.** The engine is plain scalar float code, one voice at a time, like Korg's `microkorg2/waves`. Korg's `vox` example processes 4 voices at once with NEON. Porting the engine to 4-wide NEON is the main speed-up available if CPU turns out to be tight (≈2–3× is typical for this kind of code, but that is an expectation, not a measurement).
- Mod-matrix data arrives once per buffer and is smoothed by the engine's control-rate smoothing, so it costs almost nothing.

