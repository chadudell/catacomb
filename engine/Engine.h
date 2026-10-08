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
  Engine(const Engine&) = delete; // the panel refers to this engine's sequencer
  Engine& operator=(const Engine&) = delete;

  void prepare(double sampleRate);
  // Renders n mono samples (about ±0.5 full scale at the default volume). `sidechain`
  // (optional, n samples) is the plugin's audio input, at the SIDECHAIN jack as ±10 V
  // per full scale, so Catacomb's own output fed back in comes out at the same level.
  void process(float* out, int n, const float* sidechain = nullptr);

  // The manual's global settings (p. 46).
  bool resetRecallsBuffer = false; // a RESET jack trigger also recalls the BUFFER
  bool unipolarCvOut = false;      // SEQ CV outs 0…+5 V instead of ±5 V

  // Output safety limiter: keeps the host output under -1 dBFS (a resonant or folded
  // patch can otherwise clip a bus). Untouched below the ceiling; no lookahead/latency.
  bool outputLimiter = true;
  static constexpr double kLimiterCeiling = 0.891; // -1 dBFS

  // Knobs and switches (the host writes these before each block) and the cables.
  Params params;
  Patch patch;

  // The sequencer and its buttons. Press/release from the audio thread only.
  Sequencer seq;
  void press(Button b) { panel.press(b, now()); }
  void release(Button b) { panel.release(b, now()); }

  // ---- Clock ---------------------------------------------------------------------------
  // Two clock lines: line 0 is the master CLOCK (normalled to CLOCK 1), line 1 can run at
  // its own rate into CLOCK 2 (standing in for a cable from a second clock).
  //   hz1 <= 0: the TEMPO knob sets line 0.   hz2 <= 0: CLOCK 2 follows CLOCK 1.
  void setClockRates(double hz1, double hz2);
  // Lock a line to a host grid: phase 0…1 through the current step (0 = on a step).
  void setClockPhase(int line, double phase) { clockPhase[line] = phase - std::floor(phase); }
  double clockPhaseOf(int line) const { return clockPhase[line]; }
  // After a jump (transport start/locate): forget the clock inputs' last state, so the
  // next tick fires a step if `fireNow` (we're on a step boundary), and doesn't otherwise.
  void armClocks(bool fireNow);
  static double tempoHz(double knob);

  // Oscillator phases, noise, filter, envelopes and the keyboard pitch back to power-on. The plugin calls
  // this when the host's transport starts, so every playback or bounce of a song
  // sounds the same, whatever was played before.
  void resetVoice();

  // MIDI note on, the hardware's way (manual p. 47): moves the quantizer's root; when
  // the sequencers are stopped, also plays.
  void noteOn(int note, double velocity);
  // MIDI note on, a keyboard's way: always fires both envelopes (with the velocity) and
  // transposes the VCO, as if a keyboard's CV were patched into VCO 1V/OCT (C3 = 0 V).
  void playNote(int note, double velocity);

  // Panel feedback for the UI (LED flashes etc.), collected since the last call.
  int takeEvents(PanelEvent* dest, int max);

  // The patch bay's outputs, as of the last rendered sample (for the UI and tests).
  double jack(Out o) const { return outs[(int)o]; }

  double sampleRate() const { return fs; }

private:
  double now() const { return (double)samplesRendered / fs; }
  double input(In i, double normal) const;
  void updateControls();
  double tick(int sub); // one oversampled sample; sub = 0…kOversample-1 within the host sample
  // Values that only change at the host rate (unless their jack carries audio): computed
  // on sub-sample 0, or every sub-sample when the jack is patched.
  double cvCache[2]{};
  double vcoHzCache = 0, mvcoHzCache = 0, foldKCache = 0, foldNormCache = 1, vcfGCache = 0;
  double sidechainOs[kOversample]{};

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
  double clockPhase[2] = {0, 0};
  double clockHz[2] = {0, 0};
  bool clk1Was = false, clk2Was = false, resetWas = false;
  bool trigWas = false, eg2TrigWas = false, syncWas = false;
  int seqTrigSamples[2] = {0, 0};
  double internalTrig = 0; // TRIGGER button / EG TRIG MIX / MIDI, as a gate voltage
  int internalTrigSamples = 0;
  double pendingVelocity = 0;
  double keyboardVolts = 0; // the normal at VCO 1V/OCT, set by playNote()

  double vcoPhase = 0, mvcoPhase = 0;
  dsp::WhiteNoise white;
  dsp::OnePoleLP noiseLp;
  dsp::DecayEnvelope eg1, eg2;
  dsp::Svf vcf;
  dsp::DcBlocker foldDc, outDc;
  double limiterEnv = 0, limiterRelease = 0.9999;
  double vcwOut = 0, vcfOut = 0;
  dsp::Downsampler2x down4to2, down2to1;
  dsp::Upsampler2x up1to2, up2to4;
};

} // namespace catacomb
