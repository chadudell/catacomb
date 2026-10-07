#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace catacomb {

using dsp::kPi;
using dsp::kTwoPi;

namespace {

// ---- Panel ranges (manual pp. 25–36; the curves are ours) -------------------------------
constexpr double kVcoLowHz = 20, kVcoHighHz = 5000;       // VCO FREQUENCY
constexpr double kMvcoLowHz = 0.05, kMvcoHighHz = 1300;   // MOD VCO FREQUENCY ("LO"…1.3 kHz)
constexpr double kCutoffLowHz = 20, kCutoffOctaves = 10;  // VCF CUTOFF 20 Hz … 20 kHz
constexpr double kDecayMin = 0.010, kDecayMax = 6.0;      // EG DECAY ~10 ms … >5 s
constexpr double kClockLowHz = 0.5, kClockHighHz = 30;    // TEMPO, steps per second
constexpr double kEgPitchOctaves = 6;                     // EG1 AMT fully up, EG at 8 V
constexpr double kMaxFmIndex = 6;                         // MOD VCO FM AMT fully up
constexpr double kTrigSeconds = 0.005;                    // SEQ TRIG pulse width
// Rising-edge detection with hysteresis. Clocks, sync and reset are logic gates;
// triggers carry their velocity in their height, so they fire on much less.
constexpr double kGateHigh = 1.0, kGateLow = 0.5;
constexpr double kTrigHigh = 0.05, kTrigLow = 0.02;

double expRange(double knob, double lo, double hi) { return lo * std::pow(hi / lo, knob); }

// The mixer's level knobs: unity at noon, ×2 fully up.
double levelGain(double knob) { return 2.0 * knob; }

// Smooth wavefolder: sin(k·u) / sin(min(k, π/2)). Small k is a clean unity-gain line;
// at k = π/2 the peaks round over; above that the wave folds back on itself, with no
// hard breakpoints anywhere (the manual's "continuous wavefolding", p. 28).
double fold(double u, double k) { return std::sin(k * u) / std::sin(std::min(k, kPi / 2)); }

// The MOD VCO's triangle, starting at 0 V and rising.
double triangle(double p) { return p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4; }

bool edge(double v, bool& was, double high, double low) {
  if (was) {
    if (v < low) was = false;
    return false;
  }
  if (v > high) {
    was = true;
    return true;
  }
  return false;
}
bool risingEdge(double v, bool& was) { return edge(v, was, kGateHigh, kGateLow); }
bool triggerEdge(double v, bool& was) { return edge(v, was, kTrigHigh, kTrigLow); }

} // namespace

Engine::Engine() { prepare(48000); }

void Engine::prepare(double sampleRate) {
  fs = sampleRate;
  fsOs = fs * kOversample;
  smoothCoef = dsp::OnePoleLP::coef(1.0 / (kTwoPi * 0.005), fs); // ~5 ms
  smoothed = params;
  // The first stage only has to keep the second stage's stopband clean, so it can be
  // gentler; the second sets the final passband (flat to ~0.45·fs).
  down4to2.design(0.25);
  down2to1.design(0.05);
  up1to2.design(0.05);
  up2to4.design(0.25);
  foldDc.prepare(fsOs, 5.0);
  outDc.prepare(fs, 5.0);
  updateControls();
}

double Engine::tempoHz(double knob) { return expRange(knob, kClockLowHz, kClockHighHz); }

void Engine::setClockRates(double hz1, double hz2) {
  clockHz[0] = hz1;
  clockHz[1] = hz2;
}

void Engine::armClocks(bool fireNow) {
  clk1Was = fireNow ? false : clockPhase[0] < 0.5;
  clk2Was = fireNow ? false : (clockHz[1] > 0 ? clockPhase[1] : clockPhase[0]) < 0.5;
}

void Engine::resetVoice() {
  vcoPhase = mvcoPhase = 0;
  white = dsp::WhiteNoise{};
  noiseLp = dsp::OnePoleLP{};
  eg1 = dsp::DecayEnvelope{};
  eg2 = dsp::DecayEnvelope{};
  vcf.reset();
  foldDc = dsp::DcBlocker{};
  outDc = dsp::DcBlocker{};
  foldDc.prepare(fsOs, 5.0);
  outDc.prepare(fs, 5.0);
  down4to2.reset();
  down2to1.reset();
  up1to2.reset();
  up2to4.reset();
  vcwOut = vcfOut = 0;
  internalTrig = 0;
  internalTrigSamples = 0;
  pendingVelocity = 0;
  trigWas = eg2TrigWas = syncWas = false;
}

void Engine::noteOn(int note, double velocity) {
  seq.rootSemis = note - 60; // only heard while quantized (Quantizer.h)
  if (!seq.isRunning()) pendingVelocity = std::max(pendingVelocity, velocity);
}

int Engine::takeEvents(PanelEvent* dest, int max) {
  const int n = std::min(max, outboxCount);
  for (int i = 0; i < n; i++) dest[i] = outbox[i];
  outboxCount = 0;
  return n;
}

double Engine::input(In i, double normal) const {
  const uint16_t cables = patch.cables[(int)i];
  if (!cables) return normal;
  double sum = 0;
  for (int o = 0; o < kNumOuts; o++)
    if (cables & (1u << o)) sum += outs[o];
  return sum;
}

// ---- Once per host sample ------------------------------------------------------------------

void Engine::updateControls() {
  for (int i = 0; i < kNumParams; i++) {
    if (paramInfo((Param)i).smooth) smoothed.v[i] += (float)(smoothCoef * (params.v[i] - smoothed.v[i]));
    else smoothed.v[i] = params.v[i];
  }
  const Params& p = smoothed;

  c.clockHz = expRange(p[Param::Tempo], kClockLowHz, kClockHighHz);
  c.vcoOct = std::log2(expRange(p[Param::VcoFreq], kVcoLowHz, kVcoHighHz));
  c.mvcoHz = expRange(p[Param::MvcoFreq], kMvcoLowHz, kMvcoHighHz);
  c.fmIndex = kMaxFmIndex * p[Param::FmAmt] * p[Param::FmAmt];

  c.vcoGain = levelGain(p[Param::VcoLvl]);
  c.ringGain = levelGain(p[Param::RingLvl]);
  c.mvcoGain = levelGain(p[Param::MvcoLvl]);
  c.noiseGain = levelGain(p[Param::NoiseLvl]);

  // NOISE TONE: CCW a lowpass sweeping down from 20 kHz to 200 Hz, CW a highpass
  // sweeping up from 20 Hz to 5 kHz; flat at noon.
  const double tone = p[Param::NoiseTone];
  c.noiseHighpass = tone > 0.5;
  const double toneHz = c.noiseHighpass ? 20 * std::pow(2.0, (tone - 0.5) * 2 * 8)
                                        : 200 * std::pow(2.0, tone * 2 * 6.64);
  c.noiseLpA = dsp::OnePoleLP::coef(toneHz, fsOs);

  c.foldKnob = p[Param::Fold];
  c.bias = p[Param::Bias];
  c.cutoffOct = std::log2(kCutoffLowHz) + kCutoffOctaves * p[Param::Cutoff];
  c.resK = 2.0 * (1.0 - 0.985 * std::pow((double)p[Param::Resonance], 0.7));
  c.mode = p[Param::FilterMode];

  c.blend = p[Param::Blend];
  c.volume = 1.5 * p[Param::Volume] * p[Param::Volume];
  c.umix1 = p[Param::UMix1Lvl];

  // Decay time = time to fall 40 dB.
  auto decay = [&](double knob) {
    const double t = expRange(knob, kDecayMin, kDecayMax);
    return std::exp(-std::log(100.0) / (t * fsOs));
  };
  c.eg1Decay = decay(p[Param::Eg1Decay]);
  c.eg2Decay = decay(p[Param::Eg2Decay]);
  c.attackStep = 8.0 / (0.0005 * fsOs);
  c.trigMix = p[Param::EgTrigMix];

  seq.corrupt[0] = p[Param::Corrupt1];
  seq.corrupt[1] = p[Param::Corrupt2];
  seq.cvRange[0] = p[Param::CvRange1];
  seq.cvRange[1] = p[Param::CvRange2];
}

void Engine::process(float* out, int n, const float* sidechain) {
  for (int i = 0; i < n; i++) {
    panel.tick(now());
    // Panel events: TRIGGER is ours; everything else goes on to the UI.
    for (int e = 0; e < panel.numEvents(); e++) {
      const PanelEvent& ev = panel.event(e);
      if (ev.type == PanelEvent::ManualTrigger) pendingVelocity = 1.0;
      else if (outboxCount < Panel::kMaxEvents) outbox[outboxCount++] = ev;
    }
    panel.clearEvents();

    updateControls();

    if (sidechain) {
      double a, b;
      up1to2.process(10.0 * sidechain[i], a, b);
      up2to4.process(a, sidechainOs[0], sidechainOs[1]);
      up2to4.process(b, sidechainOs[2], sidechainOs[3]);
    }
    double os[kOversample];
    for (int k = 0; k < kOversample; k++) {
      outs[(int)Out::Sidechain] = sidechain ? sidechainOs[k] : 0.0;
      os[k] = tick();
    }
    const double y = down2to1.process(down4to2.process(os[0], os[1]), down4to2.process(os[2], os[3]));

    // ±5 V at the VCA jack → ±0.5 at the host, leaving headroom for folding and drive.
    const double sample = outDc.process(y) * 0.1;
    out[i] = std::isfinite(sample) ? (float)sample : 0.0f;
    samplesRendered++;
  }
}

// ---- Once per oversampled sample -------------------------------------------------------------

double Engine::tick() {
  const double trigOs = kTrigSeconds * fsOs;

  // -- Clock and sequencers -----------------------------------------------------------------
  // Each line's gate goes high as its phase wraps (a step) and low halfway through.
  const bool separate2 = clockHz[1] > 0;
  for (int l = 0; l < (separate2 ? 2 : 1); l++) {
    const double hz = l == 0 && clockHz[0] <= 0 ? c.clockHz : clockHz[l];
    clockPhase[l] += hz / fsOs;
    if (clockPhase[l] >= 1) clockPhase[l] -= std::floor(clockPhase[l]);
  }
  outs[(int)Out::Clock] = clockPhase[0] < 0.5 ? 5.0 : 0.0;

  const double clk1 = input(In::Clock1, outs[(int)Out::Clock]);
  // CLOCK 1 is normalled to CLOCK 2, unless the second line runs at its own rate.
  const double clk2 = input(In::Clock2, separate2 ? (clockPhase[1] < 0.5 ? 5.0 : 0.0) : clk1);

  if (risingEdge(input(In::Reset, 0), resetWas)) {
    seq.reset();
    if (resetRecallsBuffer) seq.recallBuffer();
  }
  seq.setFlipGate(0, input(In::BitFlip1, 0) > kGateHigh);
  seq.setFlipGate(1, input(In::BitFlip2, 0) > kGateHigh);

  // EG TRIG MIX: fully CCW only SEQ1 at full velocity, fully CW only SEQ2; equal at noon.
  const double vel1 = std::min(1.0, 2 * (1 - c.trigMix));
  const double vel2 = std::min(1.0, 2 * c.trigMix);
  double velocity = pendingVelocity; // TRIGGER button / MIDI note while stopped
  pendingVelocity = 0;
  if (risingEdge(clk1, clk1Was) && seq.clock(0)) {
    seqTrigSamples[0] = (int)trigOs;
    velocity = std::max(velocity, vel1);
  }
  if (risingEdge(clk2, clk2Was) && seq.clock(1)) {
    seqTrigSamples[1] = (int)trigOs;
    velocity = std::max(velocity, vel2);
  }
  outs[(int)Out::Seq1Trig] = seqTrigSamples[0]-- > 0 ? 5.0 : 0.0;
  outs[(int)Out::Seq2Trig] = seqTrigSamples[1]-- > 0 ? 5.0 : 0.0;
  // The SEQ CV jacks may be unipolar (a global setting); the internal SEQ AMT routing
  // always gets the bipolar CV.
  const double cv1 = seq.cv(0), cv2 = seq.cv(1);
  outs[(int)Out::Seq1Cv] = unipolarCvOut ? (cv1 + 5.0) / 2.0 : cv1;
  outs[(int)Out::Seq2Cv] = unipolarCvOut ? (cv2 + 5.0) / 2.0 : cv2;

  // The internal trigger is a short gate whose height carries the velocity, so it can be
  // replaced at the TRIGGER jack (and that, in turn, at EG2 TRIG) like the hardware.
  if (velocity > 0) {
    if (internalTrig > 0) {
      // Still high from the last trigger: drop for one sample so there's a new edge.
      internalTrig = 0;
      internalTrigSamples = 0;
      pendingVelocity = std::max(pendingVelocity, velocity);
    } else {
      internalTrig = 5.0 * velocity;
      internalTrigSamples = (int)(0.001 * fsOs);
    }
  } else if (internalTrigSamples > 0 && --internalTrigSamples == 0) {
    internalTrig = 0;
  }

  const double trig = input(In::Trigger, internalTrig);
  const double trig2 = input(In::Eg2Trig, trig);
  const double trigVel = std::min(1.0, trig / 5.0);
  if (triggerEdge(trig, trigWas)) eg1.trigger(trigVel);
  if (triggerEdge(trig2, eg2TrigWas)) eg2.trigger(std::min(1.0, trig2 / 5.0));

  const double e1 = eg1.tick(c.attackStep, c.eg1Decay);
  const double e2 = eg2.tick(c.attackStep, c.eg2Decay);
  outs[(int)Out::Eg1] = e1;
  outs[(int)Out::Eg2] = e2;

  // -- Oscillators ------------------------------------------------------------------------------
  const Params& p = smoothed;
  const double eg1Oct = e1 / 8.0 * kEgPitchOctaves;

  const double mvcoOct = input(In::Mvco1VOct, 0) + p[Param::MvcoSeq2Amt] * cv2 + p[Param::MvcoEg1Amt] * eg1Oct;
  const double mvcoHz = std::min(c.mvcoHz * std::exp2(mvcoOct), 0.25 * fsOs);
  if (risingEdge(input(In::MvcoSync, 0), syncWas)) mvcoPhase = 0;
  mvcoPhase += mvcoHz / fsOs;
  mvcoPhase -= std::floor(mvcoPhase);
  const double mvco = 5.0 * triangle(mvcoPhase);
  outs[(int)Out::Mvco] = mvco;

  // Thru-zero linear FM: the instantaneous frequency swings around the carrier and may
  // go negative (the phase runs backwards), so the pitch centre never moves.
  const double vcoOct = c.vcoOct + input(In::Vco1VOct, 0) + p[Param::VcoSeq1Amt] * cv1 + p[Param::VcoEg1Amt] * eg1Oct;
  const double vcoHz = std::min(std::exp2(vcoOct), 0.25 * fsOs);
  vcoPhase += vcoHz * (1.0 + c.fmIndex * mvco / 5.0) / fsOs;
  vcoPhase -= std::floor(vcoPhase);
  const double vco = 5.0 * std::sin(kTwoPi * vcoPhase);

  const double ring = vco * mvco / 5.0;

  const double whiteV = 5.0 * white.next() * 2.4; // ≈ ±5 V-ish, ~1 V rms
  const double lp = noiseLp.process(whiteV, c.noiseLpA);
  const double noise = c.noiseHighpass ? whiteV - lp : lp;
  outs[(int)Out::Noise] = noise;

  // -- Mixer: each channel overdrives past noon (RING stays clean), then the sum saturates.
  const double mix = dsp::softKnee(vco * c.vcoGain, 5.0, 2.5) + ring * c.ringGain +
                     dsp::softKnee(mvco * c.mvcoGain, 5.0, 2.5) +
                     dsp::softKnee(noise * c.noiseGain, 5.0, 2.5);
  const double mixer = dsp::softKnee(mix, 6.0, 4.0);
  outs[(int)Out::Mixer] = mixer;

  // -- Utility mixer (patch-bay only) -------------------------------------------------------
  outs[(int)Out::UMix12] = input(In::UMix1, ring) * c.umix1 + input(In::UMix2, 0);

  // -- VCW and VCF, in the ORDER the switch says ----------------------------------------------
  const int order = (int)std::lround(p[Param::Order]);

  auto runVcw = [&](double x) {
    const double amt = c.foldKnob + p[Param::FoldEg1Amt] * input(In::Fold, e1) / 8.0 + p[Param::FoldSeq1Amt] * cv1 / 5.0;
    const double k = 0.05 + 15.0 * std::pow(std::clamp(amt, 0.0, 1.2), 1.3);
    return foldDc.process(5.0 * fold(x / 5.0 + c.bias, k));
  };
  auto runVcf = [&](double x) {
    const double oct = c.cutoffOct + p[Param::CutoffEg1Amt] * input(In::Cutoff, e1) + p[Param::CutoffSeq2Amt] * cv2;
    vcf.process(x, dsp::Svf::gain(std::exp2(oct), fsOs), c.resK);
    // FILTER MODE crossfades LP → BP (BP scaled to unity passband at no resonance).
    return (1 - c.mode) * vcf.lp + c.mode * 2.0 * vcf.bp;
  };

  if (order == OrderVcfVcw) {
    vcfOut = runVcf(input(In::VcfIn, mixer));
    vcwOut = runVcw(input(In::VcwIn, vcfOut));
  } else {
    vcwOut = runVcw(input(In::VcwIn, mixer));
    vcfOut = runVcf(input(In::VcfIn, order == OrderVcwVcf ? vcwOut : mixer));
  }

  // -- VCAs (EG2 normalled to both), BLEND, VOLUME -------------------------------------------
  const double vcaW = vcwOut * std::clamp(input(In::VcwVcaCv, e2) / 8.0, 0.0, 1.25);
  const double vcaF = vcfOut * std::clamp(input(In::VcfVcaCv, e2) / 8.0, 0.0, 1.25);
  const double blend = std::clamp(c.blend + input(In::Blend, 0) / 10.0, 0.0, 1.0);
  const double vca = ((1 - blend) * vcaW + blend * vcaF) * c.volume;
  outs[(int)Out::Vca] = vca;
  return vca;
}

} // namespace catacomb
