# FORMANT MUTATOR

A formant-synthesis oscillator for the **Korg NTS-1 digital kit mkII** and the **Korg microKORG 2**, built on Korg's official **logue SDK**.

Two anti-aliased oscillators feed a four-band morphing formant bank (A → E → I → O → U), followed by mode-dependent distortion. Four modes — **TALK**, **GROWL**, **METAL**, **GHOST** — each change the actual signal path, not just a preset. It is tuned for talking Reese basses, neuro growls, jungle vocal stabs, robot voices and hollow pads.

```
osc1 + osc2 (+ sub / ring / phase-mod / breath, by mode)
  → harmonic shaping (BRIGHT waveform morph, GROWL pre-drive)
  → 4-band parallel formant bank (TPT state-variable band-passes)
  → nonlinear stage (soft / asymmetric / wavefold, by mode)
  → DC blocker → anti-alias LPF → safety soft clip → out
```

Both versions run the same DSP engine. On the microKORG 2 it is **polyphonic** (up to 8 voices, each with its own engine), and it gets three extra parameters plus mod-matrix destinations.

## Download & install

1. Go to [**Releases**](https://github.com/TwistedGenetics/Formant_Mutator-/releases/latest) and download the file for your synth:

   | Synth | File |
   |---|---|
   | NTS-1 mkII | `FormantMutator.nts1mkiiunit` |
   | microKORG 2 (firmware 2.0.0 or later) | `FormantMutator.mk2unit` |

2. Open **KORG KONTROL Editor** with the synth connected.
3. Drag the file onto the **oscillator** list, then press **SEND**.
4. Select the oscillator. On the NTS-1 mkII it is listed as **FORMANT MUTATOR**; on the microKORG 2 the name is **FMutator** (that platform allows 8 characters).

Each release also has `FormantMutator-preset-renders.zip`, WAV previews of the eight presets.

## NTS-1 mkII controls

Edit parameters: hold **OSC** and turn the encoder to choose, then use Knob B to set the value.

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

The SDK allows ten parameters for an NTS-1 mkII oscillator (2 knobs + 8 edit slots), and all ten are used.

## microKORG 2 controls

The microKORG 2 SDK allows 13 oscillator parameters, shown on three pages (3 + 5 + 5). The LFO and the formant envelope get separate controls, so you can use both at once.

| Page | Name | Range | Default | What it does |
|---|---|---|---|---|
| 1 | VOWEL | 0–1023 | 0 | Morphs A → E → I → O → U |
| 1 | GROWL | 0–1023 | 256 | Growl macro (per mode) |
| 1 | MODE | TALK / GROWL / METAL / GHOST | TALK | Synthesis architecture |
| 2 | SHIFT | −24…+24 semitones | 0 | Formant shift, independent of note pitch |
| 2 | RESO | 0–100 % | 50 | Formant sharpness |
| 2 | DETUNE | 0–100 % | 20 | Osc 2 detune; inharmonic ratio in METAL |
| 2 | BRIGHT | 0–100 % | 50 | Waveform morph |
| 2 | DRIVE | 0–100 % | 20 | Drive into the mode's shaper |
| 3 | LFO RATE | 0–100 % | 35 | Internal LFO 0.05–51 Hz (shape per mode) |
| 3 | LFO DPTH | −100…+100 % | 0 | LFO depth on the vowel (negative inverts it) |
| 3 | ENV DCY | 0–100 % | 50 | Formant envelope decay, 1.5 s → 15 ms |
| 3 | ENV DPTH | −100…+100 % | 0 | Note-on formant jump: **+** barks up, **−** dips down |
| 3 | SUB | 0–100 % | 50 | Sub-octave level (50 = the mode's default, 100 = double) |

**Mod matrix destinations.** The unit offers six destinations to the microKORG 2's modulation sources, per voice: **Vowel, Growl, Shift** (±2 octaves at full depth), **Reso, Bright, Drive**. Routing an envelope or LFO to Vowel gives every note its own talking movement.

Each microKORG 2 voice has its own formant envelope (retriggered by that voice's note-on) and its own internal LFO.

## Documentation

- [docs/MANUAL.md](docs/MANUAL.md): every parameter, mode and modulation source
- [docs/PRESETS.md](docs/PRESETS.md): eight patches, mostly jungle and DnB (with microKORG 2 settings)
- [docs/BUILD.md](docs/BUILD.md): building yourself (`./build.sh`, Docker on Windows, GitHub Actions)
- [docs/OPTIMISATION.md](docs/OPTIMISATION.md): DSP trade-offs, memory, CPU estimate, disabled features
- [docs/TEST_PLAN.md](docs/TEST_PLAN.md): offline test results and the on-device checklists

## Repository layout

```
formant_mutator/        NTS-1 mkII unit project (logue SDK 2.0)
  formant_engine.h      DSP engine with no SDK dependencies (shared by both units + test harness)
  fm_params.h           parameter IDs, ranges and defaults (shared)
  header.c, unit.cc     NTS-1 mkII unit header and callbacks
  config.mk, Makefile   official SDK build files
microkorg2/             microKORG 2 unit project (logue SDK 2.1, 8-voice)
  header.c, unit.cc     13 parameters, per-voice engines, mod-matrix destinations, voice events
  config.mk, Makefile   official SDK build files
test/                   offline WAV-rendering harness and automated tests
build.sh                one-command build of both units (fetches toolchains + logue SDK into .deps/)
.github/workflows/      builds, tests and publishes a GitHub Release on every push to main
```

## Status

- **NTS-1 mkII:** built with the official logue SDK Makefile using Arm GCC 10.3.1 (the 10.3-2021.10 release the SDK specifies). The ELF header and unit header match Korg's own `waves` example.
- **microKORG 2:** built by GitHub Actions with Korg's own `arm-unknown-linux-gnueabihf` toolchain (the archive and checksum the logue SDK's Docker setup uses). The result is an 18 KB ARM ELF shared object (limit 48 KB) exporting every SDK callback. The glue code follows Korg's `microkorg2/waves` and `vox` examples.
- The offline harness runs the exact DSP code, including an 8-voice microKORG 2 simulation, and every automated test passes. See [docs/test_report.txt](docs/test_report.txt).
- **v1.2.0 (microKORG 2):** v1.1.1 still crackled on single notes. The microKORG 2 build now renders voices four at a time in NEON SIMD lanes, as Korg's own `vox` oscillator does. On the desktop profiler (SSE2) that is about 40 % fewer instructions per voice than v1.1.1. NEON has single-instruction select, min and max, which SSE2 lacks, so the saving on the synth should be bigger, but that hasn't been measured. The renderer matches the scalar engine to −36 dB or better (tested). Idle voices are skipped, and the release includes **FMutator-diagnostic.mk2unit** for troubleshooting.
- **v1.1.1:** level staging fixed on both synths (even levels across keys and waveforms, clean DRIVE 0).
- **Neither build has been confirmed on hardware yet.** Loading, CPU load (especially 8 voices on the microKORG 2) and feel are covered by the checklists in [docs/TEST_PLAN.md](docs/TEST_PLAN.md).
