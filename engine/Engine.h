// Catacomb — the whole instrument: clock → sequencers → envelopes → voice, wired
// through the patch bay (manual pp. 58–59). Mono. Everything runs at 4× the host rate
// and is decimated back down; signals between modules are in volts.
//
// Real-time safe: prepare() is the only call that may allocate (it doesn't today).
#pragma once

#include "Dsp.h"
#include "Jacks.h"
#include "Panel.h"
#include "Params.h"
#include "Sequencer.h"

namespace catacomb {

class Engine {
public:
  static constexpr int kOversample = 4;

  Engine();

  void prepare(double sampleRate);
  // Renders n mono samples (about ±0.5 full scale at the default volume).
  void process(float* out, int n);

  // Knobs and switches (the host writes these before each block) and the cables.
  Params params;
  Patch patch;

  // The sequencer and its buttons. Press/release from the audio thread only.
  Sequencer seq;
  void press(Button b) { panel.press(b, now()); }
  void release(Button b) { panel.release(b, now()); }

  // MIDI note on (manual p. 47): moves the quantizer's root; when stopped, also plays.
  void noteOn(int note, double velocity);

  // Panel feedback for the UI (LED flashes etc.), collected since the last call.
  int takeEvents(PanelEvent* dest, int max);

  // The patch bay's outputs, as of the last rendered sample (for the UI and tests).
  double jack(Out o) const { return outs[(int)o]; }

  double sampleRate() const { return fs; }

private:
  double now() const { return (double)samplesRendered / fs; }
  double input(In i, double normal) const;
  void updateControls();
  double tick(); // one oversampled sample

  Panel panel{seq};
  PanelEvent outbox[Panel::kMaxEvents];
  int outboxCount = 0;

  double fs = 48000, fsOs = 192000;
  long long samplesRendered = 0;

  // Knob smoothing (5 ms one-pole on each continuous knob).
  Params smoothed;
  double smoothCoef = 0.01;

  // Per host-sample control values, derived from the smoothed knobs.
  struct Controls {
    double clockHz, vcoOct, mvcoHz, fmIndex;
    double vcoGain, ringGain, mvcoGain, noiseGain, noiseLpA;
    bool noiseHighpass;
    double foldKnob, bias, cutoffOct, resK, mode;
    double blend, volume, umix1;
    double eg1Decay, eg2Decay, attackStep;
    double trigMix;
  } c{};

  // State
  double outs[kNumOuts]{};
  double clockPhase = 0;
  bool clockHigh = false;
  bool clk1Was = false, clk2Was = false, resetWas = false;
  bool trigWas = false, eg2TrigWas = false, syncWas = false;
  int seqTrigSamples[2] = {0, 0};
  double internalTrig = 0; // TRIGGER button / EG TRIG MIX / MIDI, as a gate voltage
  int internalTrigSamples = 0;
  double pendingVelocity = 0;

  double vcoPhase = 0, mvcoPhase = 0;
  dsp::WhiteNoise white;
  dsp::OnePoleLP noiseLp;
  dsp::DecayEnvelope eg1, eg2;
  dsp::Svf vcf;
  dsp::DcBlocker foldDc, outDc;
  double vcwOut = 0, vcfOut = 0;
  dsp::Downsampler2x down4to2, down2to1;
};

} // namespace catacomb
