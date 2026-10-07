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
plugin/build.sh install    # build the AU, install it, validate with auval
```

Needs Xcode or the Command Line Tools and CMake (`python3 -m pip install --user cmake ninja`).
JUCE 8: set `JUCE_DIR`, or it uses `~/Documents/MeatThumb Synth/JUCE`, or fetches it.

In Logic or GarageBand: new Software Instrument track → Instrument slot →
**AU Instruments → Catacomb → Catacomb**.

The panel can be developed in a browser too: serve `ui/` (e.g. `python3 -m http.server -d ui 5180`);
without the plugin it runs on demo data.
