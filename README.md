# Catacomb

A generative semi-modular synth plugin (Audio Unit for Logic Pro and GarageBand),
inspired by the behaviour of a parallel-path analog generative synthesizer: a sine VCO and
triangle MOD VCO with thru-zero FM, ring mod and noise, a wavefolder and a state-variable
filter in parallel or series, two decay envelopes, and two mutating 8-step sequencers.

See [PLAN.md](PLAN.md) for the design and milestones.

```
engine/    the C++ engine — no JUCE, real-time safe (sequencer, button panel, …)
plugin/    the JUCE AU wrapper
ui/        the panel, a web page shown in the plugin's WebView
tests/     engine unit tests
```

## Build

```
tests/run.sh               # engine unit tests (clang only)
tests/render.sh            # render example patches to renders/*.wav
plugin/build.sh install    # build the AU, install it, validate with auval
tests/auhost.sh            # load the installed AU like Logic does: transport, MIDI, save/restore
```

Needs Xcode or the Command Line Tools and CMake (`python3 -m pip install --user cmake ninja`).
JUCE 8: set `JUCE_DIR`, or it uses `~/Documents/MeatThumb Synth/JUCE`, or fetches it.

In Logic or GarageBand: new Software Instrument track → Instrument slot →
**AU Instruments → Catacomb → Catacomb**.

- Press play in Logic: the sequencers run, locked to the project's grid (Clock Division, default
  1/16). Stopped, they don't run; MIDI notes play the voice (and transpose when quantized).
- Every knob is in Logic's automation lanes and the plugin's Controls view.
- Patch bay: drag between jacks to patch (an input with no cable runs on its normal — hover a
  jack to see it); drag a cable out of an input to move or remove it; double-click a jack to clear it.
- SIDECHAIN: pick a track in the plugin header's **Side Chain** menu, then patch SIDECHAIN into
  VCW IN / VCF IN / CLOCK 1 … to fold, filter or clock from another track.
- The whole state (knobs, both sequencers' bits and voltages, BUFFER, the random generator)
  saves with the project, so it reopens and bounces exactly the same.

The panel can be developed in a browser too: serve `ui/` (e.g. `python3 -m http.server -d ui 5180`);
without the plugin it runs on demo data.
