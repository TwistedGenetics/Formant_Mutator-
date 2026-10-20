# FORMANT MUTATOR

A formant-synthesis oscillator for the **Korg NTS-1 digital kit mkII**, built on the official **logue SDK 2.0**.

Two anti-aliased oscillators feed a four-band morphing formant bank (A → E → I → O → U), followed by mode-dependent distortion. Four modes — **TALK**, **GROWL**, **METAL**, **GHOST** — each change the actual signal path, not just a preset. It is tuned for talking Reese basses, neuro growls, jungle vocal stabs, robot voices and hollow pads.

```
osc1 + osc2 (+ sub / ring / phase-mod / breath, by mode)
  → harmonic shaping (BRIGHT waveform morph, GROWL pre-drive)
  → 4-band parallel formant bank (TPT state-variable band-passes)
  → nonlinear stage (soft / asymmetric / wavefold, by mode)
  → DC blocker → anti-alias LPF → safety soft clip → out
```

## Download & install

1. Go to [**Releases**](https://github.com/TwistedGenetics/Formant_Mutator-/releases/latest) and download `FormantMutator.nts1mkiiunit`.
2. Open **KORG KONTROL Editor** with the NTS-1 mkII connected.
3. Drag the file onto the **oscillator** list, then press **SEND**.
4. Select FORMANT MUTATOR with the OSC button. Play a low note and turn **A (VOWEL)** and **B (GROWL)**.

Each release also has `FormantMutator-preset-renders.zip`, WAV previews of the eight presets.

Edit parameters: hold **OSC** and turn the encoder to choose, then use Knob B to set the value.

## Controls

| Control | Name | Range | Default | What it does |
|---|---|---|---|---|
| Knob A | VOWEL | 0–1023 | 0 | Morphs A → E → I → O → U. The NTS-1 mkII shape LFO modulates this. |
| Knob B | GROWL | 0–1023 | 256 | Growl macro. Its job depends on the mode (see manual). |
| Edit 1 | MODE | TALK / GRWL / METL / GHST | TALK | Synthesis architecture |
| Edit 2 | SHIFT | −24…+24 semitones | 0 | Moves all formants together, independent of note pitch |
| Edit 3 | RESO | 0–100 % | 50 | Formant sharpness |
| Edit 4 | DETUNE | 0–100 % | 20 | Osc 2 detune (0.5–50 cents); in METAL, an inharmonic ratio 1.0–4.5 |
| Edit 5 | BRIGHT | 0–100 % | 50 | Waveform: sine → triangle → saw → square → narrow pulse |
| Edit 6 | MOD RATE | 0–100 % | 35 | LFO rate 0.05–51 Hz, or envelope decay 1.5 s–15 ms |
| Edit 7 | MOD DPTH | −100…+100 % | 0 | **+** internal LFO depth, **−** note-on formant envelope amount |
| Edit 8 | DRIVE | 0–100 % | 20 | Drive into the mode's shaper, with output compensation |

The SDK allows ten parameters for an oscillator unit (2 knobs + 8 edit slots), and all ten are used.

## Documentation

- [docs/MANUAL.md](docs/MANUAL.md): every parameter, mode and modulation source
- [docs/PRESETS.md](docs/PRESETS.md): eight patches, mostly jungle and DnB
- [docs/BUILD.md](docs/BUILD.md): building yourself (`./build.sh`, Docker on Windows, GitHub Actions)
- [docs/OPTIMISATION.md](docs/OPTIMISATION.md): DSP trade-offs, memory, CPU estimate, disabled features
- [docs/TEST_PLAN.md](docs/TEST_PLAN.md): offline test results and the on-device checklist

## Repository layout

```
formant_mutator/        SDK unit project (drop into logue-sdk/platform/nts-1_mkii/)
  formant_engine.h      DSP engine with no SDK dependencies (shared with the test harness)
  fm_params.h           parameter IDs, ranges and defaults (shared)
  header.c              unit header and parameter descriptors
  unit.cc               logue SDK 2.0 callbacks
  config.mk, Makefile   official SDK build files
test/                   offline WAV-rendering harness and automated tests
build.sh                one-command build (fetches ARM GCC 10.3 + logue SDK into .deps/)
.github/workflows/      builds, tests and publishes a GitHub Release on every push to main
```

## Status

- The unit is compiled with the official logue SDK Makefile using Arm GCC 10.3.1 (the 10.3-2021.10 release the SDK specifies). The ELF header and unit header match Korg's own `waves` example.
- The offline harness runs the exact DSP code, and every automated test passes. See [docs/test_report.txt](docs/test_report.txt).
- **It has not been tested on real NTS-1 mkII hardware yet.** On-device loading, CPU load and feel are covered by the checklist in [docs/TEST_PLAN.md](docs/TEST_PLAN.md).
