# FORMANT MUTATOR — Test Plan and Results

There are two layers of testing:
- **Offline** (`test/harness.cc`): the exact `formant_engine.h` DSP code compiled natively and driven like the hardware (raw parameter values, note events, block-wise render calls, pitch updated per block). It is fully automated, reproducible, and writes WAV files.
- **On-device**: a manual checklist for the NTS-1 mkII. **These steps have not been run yet**; they need the hardware.

Reproduce the offline results with `./test/run.sh 600` (10-minute soak), `./test/run.sh` (60 s), or `make -C test run`. The full output is in `docs/test_report.txt`.

| # | Requirement | How it's tested | Result (offline) |
|---|---|---|---|
| 1 | SDK compilation | Official SDK Makefile, Arm GCC 10.3.1, `make && make install`; also runs in CI | ✅ Builds with no warnings. 13,468-byte ELF32 ARM shared object; header target/API identical to Korg's `waves` |
| 2 | Plugin loading | On-device checklist A | ⏳ Needs hardware |
| 3 | Pitch accuracy across octaves | Harmonic-sum F0 estimate at E1–E7 (notes 28–88), TALK/GRWL/GHST | ✅ +0.13…+0.23 cents everywhere (tolerance ±1 c). The small positive offset is the deliberate 0.5 c osc spread at DETUNE 0. METAL is inharmonic by design and excluded |
| 4 | Smooth vowel transitions | Knob A swept 0→1023 in 1 s in 10-bit steps over 64-frame callbacks; click metric (peak 2nd difference / RMS) compared with static vowels | ✅ Sweep/static ratio 0.85–1.06 in all modes (limit 1.5), so no added discontinuities |
| 4b | Vowel identity | Spectral envelope at A1 vs table F1/F2 | ✅ All F1/F2 within ±0.14 octave of the table |
| 5 | Note-on / note-off | Formant envelope compared against an identical patch with no envelope; extra note-offs and all-note-off sent | ✅ Spectral centroid ×1.61 just after note-on, back to ×1.00 after ~1 s; no instability after offs |
| 6 | Extreme resonance stability | 980 renders: all modes, min/max of every parameter, RESO 100, DRIVE 100, hardware LFO at +1, notes 0–127 | ✅ 0 non-finite samples, worst peak 0.887 |
| 7 | Max drive and modulation | Included in 6, plus the random soak | ✅ |
| 8 | Clipping and headroom | Peaks across all tests; the 8 patch renders | ✅ Worst peak 0.918 (−0.7 dBFS); patches peak −1.6 to −7.0 dBFS with 0 % samples ≥ 0.99; worst DC 0.042 (notes 21–108) |
| 9 | CPU performance | Static code analysis + on-device checklist C | ⚠️ See OPTIMISATION.md. Host timing is informational only; **on-device measurement needed** |
| 10 | Extended operation | 600 s soak: random parameter changes, notes, velocities, fractional pitches, `reset()`, all-note-off, random 1–256 frame blocks | ✅ 28.8 M samples, 0 non-finite, peak 0.918 |

Additional automated checks: the accuracy of every math approximation against libm (see OPTIMISATION.md).

Listening renders in `test/out/` (also in the renders ZIP):
- `01`–`08`: the eight preset patches as short riffs. A harness-only amp gate is applied, standing in for the NTS-1 amp EG.
- `diag_vowel_sweep_{talk,growl,metal,ghost}.wav`: knob A up and down over 4 s.
- `diag_growl_knob_sweep.wav`, `diag_brightness_sweep.wav`.

## On-device checklist

### A. Loading
1. Load `FormantMutator.nts1mkiiunit` (from Releases) into the oscillator list with KORG KONTROL Editor and press SEND.
2. Confirm the name **FORMANT MUTATOR** appears on the NTS-1 mkII.
3. Confirm the knobs show **VOWEL** and **GROWL** and the edit menu lists MODE, SHIFT, RESO, DETUNE, BRIGHT, MOD RATE, MOD DPTH, DRIVE. MODE should display TALK/GRWL/METL/GHST.
4. Power-cycle and confirm the unit is still there.

### B. Sound and behaviour
1. Default patch, play C1–C6: pitch is in tune against a tuner (±2 cents).
2. Sweep knob A slowly, then quickly: no clicks or zipper noise.
3. Set the NTS-1 LFO to the oscillator shape target: the vowel moves.
4. Load each preset from PRESETS.md and compare it with its WAV render.
5. Set MOD DPTH to −80 and hammer notes with the arpeggiator: every note barks.
6. Set RESO 100, DRIVE 100 and GROWL max in each mode, holding notes for 5 min: no runaway, dropouts or DC thumps.
7. Pitch bend and fine-tune: does the oscillator follow? (It reads pitch from the runtime context every block, as Korg's example does.)

### C. CPU headroom
1. In the heaviest setting (METL, GROWL max, DRIVE max, DPTH +100, RATE 100), play fast arpeggios with the delay and reverb effects on, for 10 min.
2. Listen for crackles or dropouts and watch for firmware warnings.
3. If problems appear, apply the fallbacks in OPTIMISATION.md ("What still needs measuring on hardware") in order, and re-test.
