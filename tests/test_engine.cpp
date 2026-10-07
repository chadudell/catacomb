#include "check.h"

#include "../engine/Engine.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

using namespace catacomb;

// ---- Allocation counter (to prove process() never allocates) ---------------------------------
static std::atomic<bool> gCountAllocs{false};
static std::atomic<int> gAllocs{0};
void* operator new(std::size_t n) {
  if (gCountAllocs) gAllocs++;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

constexpr double kFs = 48000;

// Amplitude of frequency `hz` in x (Hann-windowed single-bin DFT).
double amplitudeAt(const std::vector<float>& x, double hz, double fs = kFs) {
  double re = 0, im = 0, wsum = 0;
  const size_t n = x.size();
  for (size_t i = 0; i < n; i++) {
    const double w = 0.5 - 0.5 * std::cos(dsp::kTwoPi * i / (n - 1));
    const double ph = dsp::kTwoPi * hz * i / fs;
    re += w * x[i] * std::cos(ph);
    im += w * x[i] * std::sin(ph);
    wsum += w;
  }
  return 2 * std::sqrt(re * re + im * im) / wsum;
}

double rms(const std::vector<float>& x) {
  double s = 0;
  for (float v : x) s += (double)v * v;
  return std::sqrt(s / x.size());
}

double db(double ratio) { return 20 * std::log10(std::max(ratio, 1e-12)); }

// Knob position for a target frequency on an exponential knob.
double knobFor(double hz, double lo, double hi) { return std::log(hz / lo) / std::log(hi / lo); }

struct Rig {
  Engine e;
  Rig() {
    e.prepare(kFs);
    e.seq.clear();
    e.params[Param::Eg2Decay] = 1; // long notes so the tone holds still while we measure
  }
  void hit() { e.press(Button::Trigger), e.release(Button::Trigger); }
  std::vector<float> render(double seconds, int block = 256) {
    std::vector<float> out((size_t)(seconds * kFs));
    for (size_t i = 0; i < out.size(); i += block) e.process(out.data() + i, (int)std::min<size_t>(block, out.size() - i));
    return out;
  }
  // Settle the knob smoothing, trigger, skip the attack, return a measurement window.
  std::vector<float> note(double seconds = 0.25) {
    render(0.05);
    hit();
    render(0.02);
    return render(seconds);
  }
};

} // namespace

// ---- Decimator ---------------------------------------------------------------------------------

TEST("half-band: flat passband, deep stopband") {
  dsp::Downsampler2x d;
  d.design(0.05);
  auto measure = [&](double f /* fraction of the input rate */) {
    d.reset();
    std::vector<float> out;
    for (int i = 0; i < 20000; i++) {
      const double a = std::sin(dsp::kTwoPi * f * (2 * i)), b = std::sin(dsp::kTwoPi * f * (2 * i + 1));
      const double y = d.process(a, b);
      if (i >= 4000) out.push_back((float)y);
    }
    return rms(out) * std::sqrt(2.0);
  };
  CHECK_NEAR(measure(0.05), 1.0, 0.01);  // 0.1 of the output rate
  CHECK_NEAR(measure(0.20), 1.0, 0.01);  // 0.4 of the output rate
  const double stop = db(measure(0.30)); // would alias to 0.4 of the output rate
  std::printf("      stopband at 0.6·fs_out: %.1f dB\n", stop);
  CHECK(stop < -90);
}

// ---- Voice -------------------------------------------------------------------------------------

TEST("home base: a clean sine at the VCO FREQUENCY") {
  Rig r;
  const double hz = 440;
  r.e.params[Param::VcoFreq] = (float)knobFor(hz, 20, 5000);
  const auto x = r.note();
  const double fund = amplitudeAt(x, hz);
  CHECK(fund > 0.2);
  CHECK(db(amplitudeAt(x, 2 * hz) / fund) < -60);
  CHECK(db(amplitudeAt(x, 3 * hz) / fund) < -50);
}

TEST("silent until triggered; the EG2 VCA decays it") {
  Rig r;
  r.e.params[Param::Eg2Decay] = 0.2f;
  CHECK(rms(r.render(0.1)) < 1e-6);
  r.hit();
  const double early = rms(r.render(0.05));
  r.render(1.0);
  CHECK(early > 0.05);
  CHECK(rms(r.render(0.05)) < early * 0.01);
}

TEST("thru-zero FM keeps the VCO's pitch centre") {
  // FM by the MOD VCO at half the carrier: the result must stay periodic at the
  // modulator's period (exponential FM would drift off).
  Rig r;
  const double fc = 400;
  r.e.params[Param::VcoFreq] = (float)knobFor(fc, 20, 5000);
  r.e.params[Param::MvcoFreq] = (float)knobFor(fc / 2, 0.05, 1300);
  r.e.params[Param::FmAmt] = 0.8f;
  const auto x = r.note(0.2);
  const size_t period = (size_t)std::lround(kFs / (fc / 2));
  double num = 0, den = 0;
  for (size_t i = 0; i + period < x.size(); i++) {
    num += x[i] * x[i + period];
    den += x[i] * x[i];
  }
  CHECK(num / den > 0.95); // still periodic (amplitude decays a little)
  // And it is genuinely modulated: sidebands appear at fc ± fm.
  CHECK(amplitudeAt(x, fc + fc / 2) > 0.02);
}

TEST("wavefolder: clean at 0, rich when folded; BIAS adds even harmonics") {
  const double hz = 220;
  auto harmonics = [&](float foldKnob, float bias) {
    Rig r;
    r.e.params[Param::VcoFreq] = (float)knobFor(hz, 20, 5000);
    r.e.params[Param::Fold] = foldKnob;
    r.e.params[Param::Bias] = bias;
    const auto x = r.note();
    std::vector<double> h;
    for (int k = 1; k <= 8; k++) h.push_back(amplitudeAt(x, k * hz));
    return h;
  };
  const auto clean = harmonics(0, 0);
  CHECK(db(clean[2] / clean[0]) < -40);

  const auto folded = harmonics(0.7f, 0);
  double odd = 0, even = 0;
  for (int k = 2; k <= 8; k++) (k % 2 ? odd : even) += folded[k - 1];
  CHECK(odd > 0.5 * folded[0]);      // lots of new (odd) harmonics
  CHECK(db(even / odd) < -40);       // symmetric folding: no even ones

  const auto biased = harmonics(0.7f, 0.4f);
  double evenB = 0;
  for (int k = 2; k <= 8; k += 2) evenB += biased[k - 1];
  CHECK(evenB > 0.1 * biased[0]);    // asymmetric folding: even harmonics appear
}

TEST("filter: lowpass cuts, bandpass picks out the cutoff") {
  auto level = [](float cutoffHz, float mode, double toneHz) {
    Rig r;
    r.e.params[Param::Blend] = 1; // the VCF path only
    r.e.params[Param::VcoFreq] = (float)knobFor(toneHz, 20, 5000);
    r.e.params[Param::Cutoff] = (float)(std::log2(cutoffHz / 20.0) / 10.0);
    r.e.params[Param::FilterMode] = mode;
    return amplitudeAt(r.note(), toneHz);
  };
  const double open = level(20000, 0, 1000);
  CHECK(db(level(125, 0, 1000) / open) < -28); // 3 octaves above a 12 dB/oct cutoff
  CHECK(db(level(1000, 1, 1000) / open) > -3); // BP centred on the tone
  CHECK(db(level(8000, 1, 1000) / open) < -12);
}

TEST("filter: high resonance boosts at the cutoff without losing the bass") {
  auto level = [](float res, double toneHz) {
    Rig r;
    r.e.params[Param::Blend] = 1;
    r.e.params[Param::VcoLvl] = 0.2f;
    r.e.params[Param::VcoFreq] = (float)knobFor(toneHz, 20, 5000);
    r.e.params[Param::Cutoff] = (float)(std::log2(1000 / 20.0) / 10.0);
    r.e.params[Param::Resonance] = res;
    return amplitudeAt(r.note(), toneHz);
  };
  CHECK(db(level(0.9f, 1000) / level(0, 1000)) > 12);
  CHECK(std::abs(db(level(0.9f, 100) / level(0, 100))) < 1.5);
}

TEST("envelopes: peak 8 V, decay times span ~10 ms to over 5 s") {
  auto timeTo40dB = [](float knob) {
    Rig r;
    r.e.params[Param::Eg1Decay] = knob;
    r.render(0.05);
    r.hit();
    float buf[1];
    double peak = 0;
    for (int i = 0; i < 10 * (int)kFs; i++) {
      r.e.process(buf, 1);
      const double v = r.e.jack(Out::Eg1);
      peak = std::max(peak, v);
      if (peak > 7.9 && v < 0.08) return i / kFs;
    }
    return 99.0;
  };
  CHECK_NEAR(timeTo40dB(0), 0.010, 0.002);
  CHECK(timeTo40dB(1) > 5.0);
}

TEST("EG TRIG MIX: SEQ1's triggers soften as the knob turns toward SEQ2") {
  auto peakFor = [](float mix) {
    Rig r;
    r.e.params[Param::EgTrigMix] = mix;
    r.e.params[Param::Tempo] = 1; // fast clock
    auto st = r.e.seq.state();
    st.mem.cells[1].on = true; // SEQ1 bit 2 only
    r.e.seq.setState(st);
    r.render(0.05); // let the knob smoothing settle first
    r.e.seq.setRunning(true);
    float buf[1];
    double peak = 0;
    for (int i = 0; i < (int)(0.2 * kFs); i++) {
      r.e.process(buf, 1);
      peak = std::max(peak, r.e.jack(Out::Eg1));
    }
    return peak;
  };
  CHECK_NEAR(peakFor(0), 8.0, 0.05);
  CHECK_NEAR(peakFor(0.5f), 8.0, 0.05);
  CHECK_NEAR(peakFor(0.75f), 4.0, 0.05);
  CHECK(peakFor(1) < 0.01);
}

TEST("sequencer drives it: one trigger per on bit at the TEMPO rate") {
  Rig r;
  r.e.params[Param::Tempo] = (float)knobFor(8, 0.5, 30); // 8 steps a second
  auto st = r.e.seq.state();
  st.mem.cells[0].on = st.mem.cells[4].on = true; // 2 of 8 bits
  r.e.seq.setState(st);
  r.e.seq.setRunning(true);
  float buf[1];
  int trigs = 0;
  bool was = false;
  for (int i = 0; i < (int)(4 * kFs); i++) { // 4 s = 32 steps = 8 triggers
    r.e.process(buf, 1);
    const bool high = r.e.jack(Out::Seq1Trig) > 1;
    trigs += high && !was;
    was = high;
  }
  CHECK(trigs == 8);
}

TEST("SEQ1 AMT at QTZ: the VCO plays the quantized sequence") {
  Rig r;
  r.e.params[Param::VcoFreq] = (float)knobFor(220, 20, 5000);
  r.e.params[Param::VcoSeq1Amt] = 1;
  r.e.params[Param::CvRange1] = 1;
  auto st = r.e.seq.state();
  st.held[0] = 7.0f / 12; // a held 7-semitone CV (a fifth, in major)
  st.mem.quantMode = kScaleMajor;
  r.e.seq.setState(st);
  const auto x = r.note();
  const double fifth = 220 * std::pow(2.0, 7.0 / 12);
  CHECK(amplitudeAt(x, fifth) > 10 * amplitudeAt(x, 220));
}

TEST("ORDER: the three routings sound different") {
  auto render = [](int order) {
    Rig r;
    r.e.params[Param::Order] = (float)order;
    r.e.params[Param::Fold] = 0.6f;
    r.e.params[Param::Cutoff] = 0.5f;
    r.e.params[Param::Blend] = 0.5f;
    return r.note(0.1);
  };
  const auto a = render(OrderParallel), b = render(OrderVcwVcf), c = render(OrderVcfVcw);
  auto diff = [](const std::vector<float>& x, const std::vector<float>& y) {
    double d = 0;
    for (size_t i = 0; i < x.size(); i++) d += std::abs(x[i] - y[i]);
    return d / x.size();
  };
  CHECK(diff(a, b) > 1e-3 && diff(a, c) > 1e-3 && diff(b, c) > 1e-3);
}

TEST("everything up: finite and bounded") {
  Rig r;
  for (int i = 0; i < kNumParams; i++) r.e.params.v[i] = paramInfo((Param)i).max;
  r.e.seq.factoryPattern();
  r.e.seq.setRunning(true);
  const auto x = r.render(2.0);
  float peak = 0;
  for (float v : x) {
    CHECK(std::isfinite(v));
    peak = std::max(peak, std::abs(v));
  }
  std::printf("      peak with everything up: %.2f\n", peak);
  CHECK(peak < 2.0f);
}

TEST("render is identical whatever the host's block size") {
  auto run = [](int block) {
    Rig r;
    r.e.params[Param::Fold] = 0.5f;
    r.e.params[Param::Corrupt1] = 0.8f;
    r.e.params[Param::CvRange1] = 0.5f;
    r.e.params[Param::VcoSeq1Amt] = 1;
    r.e.params[Param::Tempo] = 0.8f;
    r.e.seq.factoryPattern();
    r.e.seq.setRunning(true);
    return r.render(1.0, block);
  };
  const auto a = run(1), b = run(37), c = run(512);
  CHECK(a == b && b == c);
}

TEST("process() never allocates") {
  Rig r;
  r.e.seq.factoryPattern();
  r.e.seq.setRunning(true);
  std::vector<float> buf(512);
  gAllocs = 0;
  gCountAllocs = true;
  for (int i = 0; i < 200; i++) {
    if (i % 50 == 0) r.e.press(Button::Shift1), r.e.release(Button::Shift1);
    r.e.process(buf.data(), 512);
  }
  gCountAllocs = false;
  CHECK(gAllocs == 0);
}

TEST("playNote: triggers while the sequencer runs, and plays the note's pitch") {
  Rig r;
  r.e.params[Param::VcoFreq] = (float)knobFor(261.63, 20, 5000); // C4 at the knob
  r.e.params[Param::Tempo] = 0;
  r.e.seq.setRunning(true); // the sequencer runs (all bits off): notes still play
  r.render(0.05);
  r.e.playNote(67, 1.0); // G: 7 semitones up
  r.render(0.02);
  const auto x = r.render(0.2);
  const double g = 261.63 * std::pow(2.0, 7.0 / 12);
  CHECK(amplitudeAt(x, g) > 0.1);
  CHECK(amplitudeAt(x, g) > 20 * amplitudeAt(x, 261.63));
}
