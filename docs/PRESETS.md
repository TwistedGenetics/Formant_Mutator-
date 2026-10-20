# FORMANT MUTATOR — Preset Guide

These are the eight test patches from the offline harness (`test/harness.cc`). Each has a matching render in `test/out/` (or in the renders ZIP) so you can hear the target before dialling it in.

Knob values are given raw (0–1023) and as a rough knob position. The NTS-1 mkII amp EG, filter and effects are your choice; suggestions are given in plain terms.

| # | Patch | VOWEL (A) | GROWL (B) | MODE | SHIFT | RESO | DETUNE | BRIGHT | RATE | DPTH | DRIVE |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Talking Reese Bass | 300 (~30 %) | 350 (~35 %) | TALK | −5 | 60 | 45 | 50 | 28 | +60 | 30 |
| 2 | Dark Robot | 512 (50 %) | 450 (~45 %) | METL | −12 | 70 | 30 | 60 | 55 | +50 | 35 |
| 3 | Metallic Neuro Growl | 250 (~25 %) | 800 (~80 %) | GRWL | −2 | 78 | 22 | 62 | 47 | +75 | 60 |
| 4 | Ghost Choir | 600 (~60 %) | 300 (~30 %) | GHST | 0 | 45 | 35 | 20 | 30 | +40 | 10 |
| 5 | Jungle Vocal Stab | 100 (~10 %) | 250 (~25 %) | TALK | +3 | 65 | 15 | 55 | 70 | −80 | 25 |
| 6 | Sci-Fi Drone | 700 (~70 %) | 600 (~60 %) | METL | −7 | 85 | 62 | 40 | 35 | +80 | 20 |
| 7 | Hollow Sub Bass | 1023 (max) | 0 | GHST | −12 | 30 | 5 | 10 | 20 | 0 | 15 |
| 8 | Aggressive Formant Lead | 450 (~45 %) | 600 (~60 %) | GRWL | +5 | 70 | 40 | 85 | 62 | +45 | 75 |

### microKORG 2 settings

Pages 1 and 2 are the same as above (VOWEL, GROWL, MODE / SHIFT, RESO, DETUNE, BRIGHT, DRIVE). Page 3 splits the NTS-1's shared MOD knobs. SUB 50 matches the NTS-1 sound.

| # | Patch | LFO RATE | LFO DPTH | ENV DCY | ENV DPTH | SUB |
|---|---|---|---|---|---|---|
| 1 | Talking Reese Bass | 28 | +60 | 50 | 0 | 50 |
| 2 | Dark Robot | 55 | +50 | 50 | 0 | 50 |
| 3 | Metallic Neuro Growl | 47 | +75 | 50 | 0 | 60 |
| 4 | Ghost Choir | 30 | +40 | 50 | 0 | 50 |
| 5 | Jungle Vocal Stab | 35 | 0 | 70 | +80 | 50 |
| 6 | Sci-Fi Drone | 35 | +80 | 50 | 0 | 50 |
| 7 | Hollow Sub Bass | 20 | 0 | 50 | 0 | 70 |
| 8 | Aggressive Formant Lead | 62 | +45 | 60 | +30 | 50 |

microKORG 2 extras worth trying:
- **Neuro "yap" plus wobble:** in patch 3, add ENV DPTH +50 and ENV DCY 75. Every 16th note barks, and the LFO wobbles on top.
- **Per-voice talking chords:** route a microKORG 2 LFO or envelope to the **Vowel** destination. Because each voice has its own engine, held chords talk on every note.
- **Reverse bark:** ENV DPTH −70, ENV DCY 60. Formants dip on each note and swell back, a "wah-in" stab.

## 1. Talking Reese Bass
Two detuned saws (DETUNE 45 ≈ 10 cents) beat slowly while a triangle LFO (~0.35 Hz) walks the vowel between "aa" and "eh". Play long notes in the E0–E1 range. For the classic rolling-Reese motion, also route the NTS-1 mkII shape LFO to the vowel at a slow rate. Riding knob B adds buzzy formant FM. Amp EG: fast attack, full sustain.

## 2. Dark Robot
METAL mode with osc 2 at about 2× (slightly sharp) gives a vocoder-ish robot. The sample-and-hold LFO (~2 Hz) jumps between vowels and formant positions, like a robot saying random syllables. SHIFT −12 makes it big and deep. Use off-beat stabs around A1. Raising GROWL toward 700 makes it more ring-modulated and clangy.

## 3. Metallic Neuro Growl
The flagship DnB patch. GROWL mode pre-drives the source, feeds it back, and snarls the formants at the sub-octave. The key-synced sine LFO (~1.1 Hz) makes every 16th note open the same way. RESO 78 adds a metallic edge. For the neuro "talk" on the hardware, automate knob A per step, or set the NTS-1 mkII shape LFO to a tempo-synced rate. Lower DRIVE to 40 for a cleaner, more vowel-led growl.

## 4. Ghost Choir
Breath noise plus slowly drifting formants gives a hollow "ooh/aah" choir. Play slow lines around C3–C4. Pair with a long amp attack and release and plenty of reverb. VOWEL around 60 % sits between "ih" and "oh".

## 5. Jungle Vocal Stab
The negative MOD DPTH turns on the note-on formant envelope. Each hit "barks" up 1.2 octaves and falls back in about 70 ms (RATE 70), like a chopped ragga vocal. Velocity sets the bark size. Play short stabs around C3–G3 with a short amp decay and a little delay.

## 6. Sci-Fi Drone
METAL with a high inharmonic ratio (~3.2×) and sharp formants. The slow S&H LFO (RATE 35 ≈ 0.55 Hz) moves vowel and formant shift in random steps. Hold one low note (C2) and slowly turn GROWL and VOWEL. Add a long reverb.

## 7. Hollow Sub Bass
GHOST mode with everything soft: a near-triangle source, the dark "U" vowel, and SHIFT −12. Most of the energy comes from the clean sub-octave sine after the bank, with a hollow formant ring on top. Good for minimal DnB and dubby jungle. Turn VOWEL down to about 70 % for more mid presence on small speakers.

## 8. Aggressive Formant Lead
Narrow pulses (BRIGHT 85) through GROWL mode with heavy asymmetric drive and a ~4.4 Hz key-synced wobble. Play around E3–C5. Try DETUNE 60 for a thicker hoover-ish lead.

## Starting points for your own sounds

- **Classic "yoi" neuro bass:** GRWL, GROWL 600–900, RESO 70+, DPTH +60, RATE 40–55. Then play with knob A live.
- **Jungle amen-era vocal hits:** TALK, DPTH −60 to −100, RATE 60–80, SHIFT 0 to +5.
- **Talking pad:** GHST, DPTH +30, RATE 20, BRIGHT 30, long amp EG.
- **Clangy techstep stab:** METL, DETUNE 70+, GROWL 800+, DRIVE 60, DPTH −70.
