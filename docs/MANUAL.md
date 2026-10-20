# FORMANT MUTATOR — User Manual

## How it makes sound

A formant is a resonant peak in a sound's spectrum. Vowels are distinguished mostly by where their first two formants (F1 and F2) sit. FORMANT MUTATOR builds a harmonically rich source, then pushes it through four resonant band-pass filters (F1–F4) placed at vowel positions. Moving those positions produces talking, yowling and robotic movement.

Formant positions are **fixed in Hz** and do not follow the keyboard (METAL mode is the exception, see below). The same vowel therefore reads as the same vowel on every note, like a real mouth.

## Knobs

### A — VOWEL (0–1023)
Morphs continuously through **A → E → I → O → U** (about 256 steps per vowel). Each vowel defines centre frequency, bandwidth and relative gain for all four formants; the per-formant gains also set the vowel's spectral tilt. Everything is interpolated smoothly, so there are no clicks or steps.

The **NTS-1 mkII shape LFO** is added to this knob. Set the hardware LFO to the oscillator shape target for hands-free talking. If the modulation goes past either end of the range, it reflects back rather than clipping, so wide sweeps stay musical.

### B — GROWL (0–1023)
A per-mode macro. In every mode it also tightens the formants slightly (up to 35 % narrower).

| Mode | GROWL controls |
|---|---|
| TALK | Audio-rate formant FM from osc 1: the vowel buzzes and "yoi"s at the note frequency |
| GROWL | Pre-drive into the formant bank, feedback growl, and formant FM from the sub-octave (sub-harmonic snarl) |
| METAL | Ring-mod mix (osc 1 × osc 2) and phase modulation of osc 1 by osc 2 |
| GHOST | Breath noise into the formant bank (whispered vowels) and per-formant drift depth (choir beating) |

## Edit menu

### 1 MODE
Each mode changes the DSP path:

**TALK**: clear, articulate vowels.
- Source: osc 1 + osc 2 (detuned), plus a sine sub-octave fed **into** the formants.
- Formants at normal width; fast vowel response.
- Shaper: soft (tanh) saturation.
- Internal LFO: free-running **triangle**.

**GRWL (GROWL)**: aggressive DnB and neuro bass.
- Source: osc 1 + osc 2 plus a rounded-square sub, then **pre-driven** before the formants so the distortion's harmonics get carved into vowels.
- Feedback path: a low-passed formant output is fed back into the bank through tanh. The loop gain stays below 1, so it thickens but never runs away.
- Audio-rate formant FM from the sub-octave.
- A clean sub-octave is added **after** the bank for weight.
- Shaper: **asymmetric** distortion (adds even harmonics); DRIVE also raises the asymmetry.
- Internal LFO: **sine, restarted on every note-on**, so each hit wobbles the same way.

**METL (METAL)**: robotic, inharmonic, metallic.
- Osc 2 runs at an **inharmonic ratio** (DETUNE sets 1.0–4.5×, deliberately off-integer).
- GROWL blends from a sum of the oscillators to a **ring modulator**, and adds phase modulation of osc 1 by osc 2.
- Formants are about 2.5× sharper and **key-track at 50 %**, so the metallic colour stays consistent across the keyboard.
- Shaper: **sine wavefolder**, blended in by DRIVE.
- Internal LFO: **smoothed sample-and-hold** (random robot jumps). With MOD DPTH positive, it also jumps the formant shift.

**GHST (GHOST)**: hollow pads, drones and choirs.
- Formants are about 1.7× wider (breathy), each with its own **slow independent drift** (0.13–0.31 Hz), which beats like a choir.
- Breath noise (GROWL) is filtered by the formants into whispered vowels.
- No dry or sub signal inside the bank. A clean sine sub-octave sits **after** it, which gives the hollow sub sound.
- Vowel changes glide slowly (~0.1 s), even from knob A.
- Internal LFO: slow free **sine** at a quarter of the MOD RATE speed.
- Shaper: soft saturation.

### 2 SHIFT (−24…+24 semitones)
Moves every formant together, independent of the note. Negative values give deeper, bigger, more robotic voices; positive values give smaller, synthetic or chipmunk voices. Bandwidths scale with the formants, keeping a constant Q.

### 3 RESO (0–100 %)
Formant sharpness. At 0 % the formants are broad and soft; at 100 % they are narrow and whistling. A **pitch-aware bandwidth floor** stops formants getting narrower than the harmonic spacing (otherwise a bass note's harmonics would fall between them and the vowel would vanish). High RESO lowers this floor so the formants can ring. Narrow settings get partial gain makeup.

### 4 DETUNE (0–100 %)
- TALK, GROWL, GHOST: spreads osc 1 down and osc 2 up, from 0.5 to 50 cents total on a squared curve. Low values give slow chorus; 40–60 % gives a classic Reese beat.
- METAL: the ratio of osc 2, from 1.0 to 4.5×, slightly off-integer.

### 5 BRIGHT (0–100 %)
Harmonic content of both main oscillators:

| Range | Waveform |
|---|---|
| 0–10 % | sine → triangle |
| 10–45 % | triangle → saw (pure saw at 45 %) |
| 45–75 % | saw → square (pure square at 75 %) |
| 75–100 % | square → narrow pulse (50 % down to 8 % width) |

Saw and pulse are polyBLEP anti-aliased. A pure sine has no harmonics for formants to shape, so below about 10 % the oscillator gets quiet on low notes. In GHOST and GROWL the sub still sounds.

### 6 MOD RATE (0–100 %)
- With **MOD DPTH > 0**: internal LFO rate, exponential from 0.05 Hz to 51 Hz. In GHOST the LFO runs at a quarter of this speed.
- With **MOD DPTH < 0**: formant envelope **decay time**, from 1.5 s at 0 % down to 15 ms at 100 %.

### 7 MOD DPTH (−100…+100 %)
One knob for two modulation sources:
- **Positive**: internal LFO depth on the vowel position (up to ±half the vowel range). In GROWL it also wobbles the formant shift slightly; in METAL the random steps also jump the formant shift.
- **Negative**: **note-on formant envelope** amount. On each note the formants jump up (by up to +1.5 octaves at −100 %) with a small vowel push, then fall back over the decay time. Velocity scales it from 35 % to 100 %. This gives the "bark" of a jungle vocal stab or the yap of a neuro bass.
- **0**: no internal modulation. The hardware shape LFO still works.

### 8 DRIVE (0–100 %)
Gain into the mode's shaper (×1 up to ×16), with automatic output compensation so louder settings get dirtier but not much louder.

## Modulation sources summary

| Source | Target | Set by |
|---|---|---|
| NTS-1 mkII shape LFO | Vowel position | NTS-1 mkII LFO settings |
| Internal LFO (triangle / sine key-sync / S&H / slow sine) | Vowel (+ shift in GROWL/METAL) | MOD RATE, MOD DPTH > 0, MODE |
| Formant envelope (≈2 ms attack, exponential decay) | Formant shift (+ small vowel push) | MOD RATE, MOD DPTH < 0, velocity |
| Audio-rate formant FM | Formant frequencies, per sample | GROWL knob (TALK, GROWL, METAL) |
| Phase modulation osc 2 → osc 1 | Oscillator phase | GROWL knob (METAL) |
| Ring modulation | Source spectrum | GROWL knob (METAL) |
| GHOST drift LFOs | Each formant independently | GROWL knob (GHOST) |

## Notes and behaviour

- Pitch is read from the NTS-1 mkII runtime at the start of every audio block (note plus fine pitch), the same way Korg's reference oscillator reads it.
- Every note-on retriggers the formant envelope. In GROWL mode it also restarts the LFO. Oscillator phases are never reset, which avoids clicks when retriggering inside the amp release.
- The oscillator does not use the audio input.
- Output peaks stay under 0 dBFS. Typical patches peak around −2 to −7 dBFS before the NTS-1 mkII filter and amp.

## microKORG 2 version

The microKORG 2 build uses the same engine, sound and modes, with these differences:

- **Polyphonic.** Every voice the microKORG 2 assigns (up to 8) runs its own engine, with its own oscillator phases, formant filters, envelope and LFO. The voice output is trimmed (×0.4) because the synth sums voices; Korg's own `waves` example uses ×0.3.
- **No knobs A/B or shape LFO.** All 13 parameters sit on the oscillator's three parameter pages: VOWEL, GROWL, MODE / SHIFT, RESO, DETUNE, BRIGHT, DRIVE / LFO RATE, LFO DPTH, ENV DCY, ENV DPTH, SUB.
- **Separate LFO and envelope.** LFO RATE and LFO DPTH control only the internal LFO; LFO DPTH is bipolar (negative inverts the LFO). ENV DCY and ENV DPTH control only the note-on formant envelope. ENV DPTH is bipolar: positive values make formants jump up and fall back (the NTS-1 "bark"); negative values make them dip down and rise back. On the NTS-1 mkII one knob pair has to share both jobs.
- **SUB.** Scales the sub-octave in every mode where the mode uses one (TALK into the bank, GROWL before and after the bank, GHOST after the bank). 50 is the NTS-1 level, 0 removes it, 100 doubles it. METAL has no sub.
- **Velocity.** The envelope amount follows velocity the same way as on the NTS-1 (35 % at velocity 0, 100 % at 127).
- **Mod matrix destinations.** Six destinations are offered to the microKORG 2's modulation sources:

  | Destination | Full-depth range |
  |---|---|
  | Vowel | the whole A–U range, like turning VOWEL |
  | Growl | the whole GROWL range |
  | Shift | ±2 octaves of formant shift |
  | Reso | the whole RESO range |
  | Bright | the whole BRIGHT range |
  | Drive | the whole DRIVE range |

  Modulation is added to the parameter value per voice and smoothed, so audio-rate stepping doesn't click. The vowel reflects at the ends of its range like the NTS-1 shape LFO.

