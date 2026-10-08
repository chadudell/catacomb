# Catacomb — plan

A generative semi-modular synth plugin (Audio Unit, for Logic Pro and GarageBand),
modelled on the behaviour of a generative semi-modular synthesizer.

**Ground rules**
- Catacomb copies *behaviour*, not trade dress: no product names, logos or panel
  artwork in the product. Our own layout and graphics. "Inspired by" only.
- One mono voice driven by its own sequencers (like the hardware).
- No hardware to measure against, so curves the manual doesn't specify are tuned by ear
  and listed under "Our choices" below.

## Decisions

| | |
|---|---|
| Name | Catacomb |
| Format | AU only (`aumu`), Logic Pro + GarageBand. Standalone app for quick testing. |
| UI | Web UI (HTML/SVG/JS) in a WebView, as in Meat Thumb |

## Architecture

```
┌──────────── Plugin (JUCE: AU + Standalone) ────────────────────┐
│ Processor: params ⇄ engine, host transport/tempo, MIDI, state  │
│ Editor:    WebView panel (HTML/SVG), message bridge            │
└───────────────┬────────────────────────────────────────────────┘
                │
┌──── engine/ (pure C++17, no JUCE, real-time safe, unit-tested) ┐
│ Clock → Sequencer (2 heads, chain, corrupt, quantizer)          │
│ Panel: button presses/holds → sequencer actions (hardware combos)│
│ EG TRIG MIX → EG1, EG2                                          │
│ VCO(sine, thru-zero FM) · MOD VCO(tri, sync) · RING · NOISE     │
│ → saturating MIXER → ORDER router → VCW / VCF → 2×VCA → BLEND   │
│ → VOLUME                                                        │
│ Every jack is a named signal (volts); normalled connections are │
│ the default routes, so the patch bay is only routing.           │
└─────────────────────────────────────────────────────────────────┘
```

Signals are floats in volts: ±5 V audio/CV, 0–8 V envelopes, 0/5 V triggers.

## Sequencer (the heart)

| Feature | Behaviour |
|---|---|
| Bits | 8 per sequencer; flipping a bit on stores a fresh random value in ±5 V |
| Heads | Play + write head. BIT SHIFT+ADVANCE offsets the write head (+RESET returns it). RESET keeps the offset. |
| LENGTH | 8→7→…→1→8. LENGTH+RESET → 8 |
| BIT SHIFT | Rotate right within the current length. BIT SHIFT+RESET undoes all rotation. |
| BIT FLIP | XOR at the write head; source = button OR CV input OR corrupt. Held = flips each step. |
| CORRUPT | 0→noon: voltage-change chance 0→25%. Noon→max: voltage 25→50%, bit-flip 0→50%. At the write head. |
| CV RANGE | Attenuates before the quantizer; bipolar around the root |
| Quantizer | 16 modes: Unquantized, Chromatic, Major, Pentatonic, Melodic minor, Harmonic minor, Diminished 6th, Whole tone, Hirajoshi, 7sus4, Maj7, Maj13, Min7, Min11, Hang drum, Quads |
| CHAIN SEQ | One loop of up to 16 steps, both play heads on it. In chain mode BIT SHIFT 1 rotates all, BIT SHIFT 2 moves the SEQ2 play head (+RESET resyncs). |
| BUFFER | Hold 1 s = save; tap = recall. One slot (bits, voltages, quant mode, write offsets, lengths, chain). BUFFER+RESET hold = clear. |
| Transport | RUN/STOP doesn't reset; ADVANCE only while stopped |
| MIDI note | Quantized → transposes the root; stopped → also triggers (play it like a keyboard) |

Plugin specifics: the clock follows Logic's tempo/transport, sample-accurate, with a
16-value division; a second per-sequencer division stands in for the CLOCK 2 jack. All
randomness is a seeded PRNG whose state saves with the project, so a reopened song or a
bounce plays identically. "Re-roll" deliberately breaks out of that.

## Voice DSP

| Block | Approach |
|---|---|
| VCO | Sine, ~20 Hz–5 kHz exp. Knob + 1V/oct + SEQ1×amt + EG1×amt. Thru-zero linear FM from MOD VCO. |
| MOD VCO | Triangle, sub-Hz to ~1.3 kHz, PolyBLAMP, hard-sync input |
| RING / NOISE | VCO × MOD VCO; white noise → tilt EQ (NOISE TONE) |
| Mixer | Per-channel gain 0–2, soft saturation above unity (noon); RING stays clean |
| VCW | Smooth multi-stage folder (no hard breakpoints) + BIAS; FOLD = knob + EG1/CV×amt + SEQ1×amt; oversampling + ADAA |
| VCF | 2-pole TPT SVF, LP↔BP crossfade, resonance to near self-osc without bass loss, mild feedback nonlinearity, 20 Hz–20 kHz |
| ORDER | Parallel / VCW→VCF / VCF→VCW (manual p. 59) |
| VCA/BLEND/VOLUME | Two VCAs (EG2 normalled) → BLEND crossfader (CV-able) → VOLUME |
| EG1/EG2 | Decay-only, instant attack, ~10 ms–5 s+ exponential. EG TRIG MIX sets SEQ1/SEQ2 trigger velocities. |
| Drift | Optional slow pitch drift / tolerances |

## Patch bay
All 32 jacks (20 in / 12 out) with the manual's normals (EG1→FOLD/CUTOFF, EG2→both VCA CVs,
RING→U MIX 1, CLOCK 1→CLOCK 2, TRIGGER vs EG2 TRIG). Drag-to-patch cables, multiples from
outputs, saved with the state. Extras: optional sidechain audio into VCW IN / VCF IN / U MIX;
unipolar CV-out option.

## Plugin layer
~30 knobs + ORDER, CHAIN, quant mode, RUN, clock division as automatable parameters.
Momentary buttons work from the UI; an optional keyswitch mode maps them to MIDI notes.
State = params + sequencer memory + BUFFER + RNG + cables + settings. Mono → stereo,
zero latency, real-time safe, auval clean.

## Testing
- Sequencer: every rule above and every button combo (manual p. 37); corrupt probabilities
  checked statistically.
- DSP: TZFM pitch stability (FFT), filter LP/BP response, fold symmetry, EG times, aliasing.
- Golden renders, no allocations in render, block-size independence, auval.

## Milestones

| # | Deliverable | Status |
|---|---|---|
| M0 | Skeleton: CMake/JUCE, `build.sh install`, auval; empty instrument loads in Logic | done (auval passes) |
| M1 | Sequencer engine + Panel (button combos) + tests, headless | done (47 tests) |
| M2 | Voice DSP, normals only | done (15 engine tests, `tests/render.sh`) |
| M3 | Plugin layer: params, host sync, MIDI, state | done (`tests/auhost.sh` against the installed AU) |
| M4 | Panel UI: knobs, buttons, LEDs | done |
| M5 | Patch bay: routing + cables | done (13 patch tests + host save/load check) |
| M6 | Sound pass, sidechain input, starter presets, polish | done, except tuning by ear (needs listening notes) |

## Our choices (manual is silent)
- Knob curves (exponential for frequencies/times, linear elsewhere) — by ear.
- MIDI clock division values: 1/32T … 4 bars style list (see engine).
- Corrupt probability is linear in the knob within each half.
- Mutations (flip/corrupt) at the write head apply *before* the step sounds, so you hear them at once.
- ADVANCE moves the heads without firing a trigger.
- Sequence Clear resets the quantizer to mode 1 (Unquantized).
- Hang drum scale: pitch classes {0, 2, 3, 7, 8, 10} (an "Integral"-style hang); Diminished 6th
  = {0, 2, 4, 5, 7, 8, 9, 11}.
- BIT FLIP CV input is sampled on each clock (a high input flips the write bit).
- EG retrigger ramps to the new peak in 0.5 ms (no clicks) rather than jumping.
- Factory pattern: random, but 3–6 bits on in each sequencer.
- Ranges: MOD VCO 0.05 Hz–1.3 kHz; TEMPO 0.5–30 steps/s (internal clock); EG1 AMT fully up
  = ±6 octaves of pitch; FM AMT fully up = index 6; EG1/CV → CUTOFF is 1 V/oct (8 octaves
  from the EG); mixer channels clean to 5 V, then a soft knee.
- Wavefolder: sin(k·u)/sin(min(k, π/2)), k from 0.05 (clean) to ~15; 4× oversampling.
- Output: ±5 V at the VCA jack = ±0.5 at the host.
- Clock: *Host Tempo* (default) locks steps to Logic's grid while it plays (bar 1 beat 1 = bit 1)
  and free-runs at the project tempo while stopped; *Tempo Knob* is the hardware's internal clock.
  *Follow Transport* (default on) starts/stops the sequencers with Logic.
- Transport start resets the voice (oscillator phases, noise, filter, envelopes) so every
  playback and bounce of a song sounds the same.
- The quantizer's MIDI root is saved with the project.
- Patch bay: our own arrangement (one row per function). 32 jacks = the hardware's 31 (MIDI is
  the host's) + SIDECHAIN, the plugin's audio input (0 dBFS = ±10 V). Inputs sum multiple cables;
  patching reads outputs one oversampled sample late (5 µs), so feedback patches are fine.
- Gate inputs (clocks, sync, reset, BIT FLIP) have hysteresis: high above 1 V, low below 0.5 V.
  Trigger inputs fire above 0.05 V (their height is the velocity).
- The two global settings (RESET recalls BUFFER, unipolar CV outs) are plugin parameters.
- MIDI Notes (plugin parameter): *Play* (default) — every note fires both EGs and sets the VCO's
  1V/OCT normal (C3 = 0 V), sequencer running or not; it resets to 0 V on transport start.
  *Transpose only* — the hardware's behaviour. (The hardware's way alone left Logic regions and
  Live Loops silent whenever the transport ran.)
- CPU: pitch, cutoff and fold curves run at the host rate (per oversampled sample only when
  their jack is patched); the quantizer reruns only on a step or a host sample. ~2–3% of a core.
- Output limiter (on by default): instant-attack / 150 ms-release peak follower, ceiling -1 dBFS,
  no latency; transparent below the ceiling.
- 13 factory presets (engine/Presets.cpp), after the manual's patch notes, our own names. A preset
  sets knobs (others to default), step division, bits/scale/lengths, cables, and seeds the RNG;
  BUFFER holds the preset's pattern. It leaves MIDI mode, follow transport, clock source,
  limiter and the global settings alone.
- UI must be checked in WebKit (tests/snap.swift), not just Chromium: WebKit mis-centres
  script-built grid items, so grid cells stretch and centre their own content.
- Panel extras the hardware doesn't have: click a bit LED to flip it; menus for scale and clock;
  a value readout while turning a knob; Re-roll notes (new voltages, same rhythm).
- Scale (quant mode), CHAIN and RUN aren't host parameters: they live in the sequencer's memory
  (BUFFER saves them), so the panel and BUFFER stay the single source of truth.
