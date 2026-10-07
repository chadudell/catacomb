#include "check.h"

#include "../engine/Engine.h"
#include "../engine/StateText.h"

#include <cstring>
#include <vector>

using namespace catacomb;

namespace {

constexpr double kFs = 48000;

double knobFor(double hz, double lo, double hi) { return std::log(hz / lo) / std::log(hi / lo); }

double rms(const std::vector<float>& x) {
  double s = 0;
  for (float v : x) s += (double)v * v;
  return std::sqrt(s / (double)x.size());
}

double amplitudeAt(const std::vector<float>& x, double hz) {
  double re = 0, im = 0, wsum = 0;
  const size_t n = x.size();
  for (size_t i = 0; i < n; i++) {
    const double w = 0.5 - 0.5 * std::cos(dsp::kTwoPi * (double)i / (double)(n - 1));
    const double ph = dsp::kTwoPi * hz * (double)i / kFs;
    re += w * x[i] * std::cos(ph);
    im += w * x[i] * std::sin(ph);
    wsum += w;
  }
  return 2 * std::sqrt(re * re + im * im) / wsum;
}

struct Rig {
  Engine e;
  Rig() {
    e.prepare(kFs);
    e.seq.clear();
  }
  std::vector<float> render(double seconds, const float* sidechain = nullptr) {
    std::vector<float> out((size_t)(seconds * kFs));
    for (size_t i = 0; i < out.size(); i += 256) {
      const int n = (int)std::min<size_t>(256, out.size() - i);
      e.process(out.data() + i, n, sidechain ? sidechain + i : nullptr);
    }
    return out;
  }
  // Counts rising edges at an output jack over `seconds`.
  int edges(Out o, double seconds) {
    float buf[1];
    int n = 0;
    bool was = false;
    for (int i = 0; i < (int)(seconds * kFs); i++) {
      e.process(buf, 1);
      const bool high = e.jack(o) > 1;
      n += high && !was;
      was = high;
    }
    return n;
  }
};

} // namespace

// ---- Resampling -----------------------------------------------------------------------------

TEST("half-band upsampler: passes the tone, rejects its image") {
  dsp::Upsampler2x up;
  up.design(0.05);
  const double f = 0.1; // of the input rate
  std::vector<float> out;
  for (int i = 0; i < 20000; i++) {
    double a, b;
    up.process(std::sin(dsp::kTwoPi * f * i), a, b);
    if (i >= 2000) out.push_back((float)a), out.push_back((float)b);
  }
  // At the doubled rate (2·kFs as the "rate" for amplitudeAt): tone at f·rate_in.
  auto at = [&](double frac) { // frac of the doubled rate
    double re = 0, im = 0;
    for (size_t i = 0; i < out.size(); i++) {
      re += out[i] * std::cos(dsp::kTwoPi * frac * (double)i);
      im += out[i] * std::sin(dsp::kTwoPi * frac * (double)i);
    }
    return 2 * std::sqrt(re * re + im * im) / (double)out.size();
  };
  CHECK_NEAR(at(f / 2), 1.0, 0.01);
  CHECK(at(0.5 - f / 2) < 1e-4); // the image would sit here
}

// ---- Cables as text -------------------------------------------------------------------------

TEST("cables: round trip, order kept, junk and duplicates dropped") {
  const std::vector<Cable> cables = {{Out::Eg2, In::BitFlip1}, {Out::Clock, In::Vco1VOct}, {Out::Mvco, In::Blend}};
  const auto text = encodeCables(cables);
  CHECK(text == "eg2>bitFlip1,clock>vco1voct,mvco>blend");
  const auto back = decodeCables(text + ",nope>fold,eg2>bitFlip1,mixer>");
  CHECK(back.size() == 3);
  CHECK(back[1].out == Out::Clock && back[1].in == In::Vco1VOct);
  CHECK(patchFrom(back).patched(In::Blend));
}

// ---- Normals and overrides ------------------------------------------------------------------

TEST("patch: a cable into VCW VCA CV replaces EG2 (sound with no trigger)") {
  Rig r;
  CHECK(rms(r.render(0.2)) < 1e-6);
  r.e.params[Param::MvcoFreq] = (float)knobFor(2, 0.05, 1300);
  r.e.patch.connect(Out::Mvco, In::VcwVcaCv); // a slow triangle opens the VCA
  CHECK(rms(r.render(1.0)) > 0.02);
}

TEST("patch: SEQ TRIG out → TRIGGER in fires the envelopes on SEQ2's rhythm, ignoring EG TRIG MIX") {
  Rig r;
  r.e.params[Param::Tempo] = (float)knobFor(8, 0.5, 30);
  r.e.params[Param::EgTrigMix] = 0; // normally only SEQ1 would trigger
  auto st = r.e.seq.state();
  st.mem.cells[8 + 2].on = true; // SEQ2 only
  r.e.seq.setState(st);
  r.e.seq.setRunning(true);
  CHECK(r.edges(Out::Seq2Trig, 2.0) == 2);
  r.e.patch.connect(Out::Seq2Trig, In::Trigger);
  float peak = 0, buf[1];
  for (int i = 0; i < (int)(2 * kFs); i++) {
    r.e.process(buf, 1);
    peak = std::max(peak, (float)r.e.jack(Out::Eg2));
  }
  CHECK(peak > 7.5f);
}

TEST("patch: with EG2 TRIG patched, TRIGGER only fires EG1") {
  Rig r;
  r.e.patch.connect(Out::Noise, In::Eg2Trig); // anything; it just has to be patched
  r.e.patch.disconnect(Out::Noise, In::Eg2Trig);
  r.e.patch.connect(Out::Seq2Trig, In::Eg2Trig); // a jack that stays low here
  r.e.press(Button::Trigger), r.e.release(Button::Trigger);
  float buf[1];
  double eg1 = 0, eg2 = 0;
  for (int i = 0; i < 4800; i++) {
    r.e.process(buf, 1);
    eg1 = std::max(eg1, r.e.jack(Out::Eg1));
    eg2 = std::max(eg2, r.e.jack(Out::Eg2));
  }
  CHECK(eg1 > 7.5 && eg2 < 0.01);
}

TEST("patch: EG2 → BIT FLIP 1 rewrites SEQ1 as it plays (manual's Syndrums tip)") {
  Rig r;
  r.e.params[Param::Tempo] = (float)knobFor(8, 0.5, 30);
  r.e.params[Param::EgTrigMix] = 0.5f;
  r.e.params[Param::Eg2Decay] = 0.6f;
  r.e.seq.factoryPattern();
  r.e.seq.setRunning(true);
  const auto before = r.e.seq.memory();
  r.e.patch.connect(Out::Eg2, In::BitFlip1);
  r.render(3.0);
  int changed = 0;
  for (int c = 0; c < 8; c++) changed += before.cells[c].on != r.e.seq.memory().cells[c].on;
  CHECK(changed > 0);
}

TEST("patch: an external clock at CLOCK 1 overrides the internal one (and feeds CLOCK 2)") {
  Rig r;
  r.e.params[Param::Tempo] = 0; // internal clock very slow
  r.e.params[Param::MvcoFreq] = (float)knobFor(5, 0.05, 1300);
  r.e.patch.connect(Out::Mvco, In::Clock1); // a 5 Hz triangle as the clock
  r.e.seq.setRunning(true);
  int steps1 = 0, steps2 = 0, last1 = r.e.seq.playStep(0), last2 = r.e.seq.playStep(1);
  float buf[1];
  for (int i = 0; i < (int)(2 * kFs); i++) {
    r.e.process(buf, 1);
    steps1 += r.e.seq.playStep(0) != last1;
    steps2 += r.e.seq.playStep(1) != last2;
    last1 = r.e.seq.playStep(0);
    last2 = r.e.seq.playStep(1);
  }
  CHECK(steps1 >= 9 && steps1 <= 11);
  CHECK(steps2 == steps1);
}

TEST("patch: CLOCK 2 patched separately runs SEQ2 on its own clock") {
  Rig r;
  r.e.params[Param::Tempo] = (float)knobFor(8, 0.5, 30);
  r.e.params[Param::MvcoFreq] = (float)knobFor(2, 0.05, 1300);
  r.e.patch.connect(Out::Mvco, In::Clock2);
  r.e.seq.setRunning(true);
  r.render(2.0);
  // SEQ1 made ~16 steps (wraps to 0), SEQ2 ~4.
  CHECK(r.e.seq.playStep(1) >= 3 && r.e.seq.playStep(1) <= 5);
}

TEST("patch: CLOCK out → VCO 1V/OCT moves the pitch (manual's hi-hat tip)") {
  auto pitch = [](bool patched) {
    Rig r;
    r.e.params[Param::VcoFreq] = (float)knobFor(200, 20, 5000);
    r.e.params[Param::Tempo] = 0; // 0.5 Hz: the clock's high half covers our window
    r.e.params[Param::Eg2Decay] = 1;
    if (patched) r.e.patch.connect(Out::Clock, In::Vco1VOct);
    r.e.press(Button::Trigger), r.e.release(Button::Trigger);
    r.render(0.05);
    const auto x = r.render(0.2);
    return amplitudeAt(x, 200) > amplitudeAt(x, 200 * 32) ? 200.0 : 6400.0; // +5 V = 5 octaves
  };
  CHECK(pitch(false) == 200.0);
  CHECK(pitch(true) == 6400.0);
}

TEST("patch: RING is normalled into U MIX 1; U MIX 1+2 → VCW IN replaces the mixer") {
  Rig r;
  r.e.params[Param::UMix1Lvl] = 1;
  r.e.params[Param::VcoLvl] = 0; // nothing in the main mixer
  r.e.params[Param::MvcoFreq] = (float)knobFor(100, 0.05, 1300);
  r.e.params[Param::Eg2Decay] = 1;
  r.render(0.05); // let the VCO level glide down first
  r.e.press(Button::Trigger), r.e.release(Button::Trigger);
  const auto quiet = r.render(0.2);
  CHECK(amplitudeAt(quiet, 216.23) < 1e-5 && amplitudeAt(quiet, 0) < 1e-3); // no ring tone
  r.e.patch.connect(Out::UMix12, In::VcwIn);
  const auto loud = r.render(0.2);
  CHECK(amplitudeAt(loud, 216.23) > 0.01); // ring of VCO (316 Hz) × MOD VCO (100 Hz): 216 + 416 Hz
}

TEST("patch: the sidechain reaches the voice through SIDECHAIN → VCF IN") {
  Rig r;
  r.e.params[Param::Blend] = 1;
  r.e.params[Param::VcoLvl] = 0;
  r.e.params[Param::Eg2Decay] = 1;
  r.e.patch.connect(Out::Sidechain, In::VcfIn);
  std::vector<float> side((size_t)kFs);
  for (size_t i = 0; i < side.size(); i++) side[i] = 0.25f * (float)std::sin(dsp::kTwoPi * 330 * (double)i / kFs);
  r.e.press(Button::Trigger), r.e.release(Button::Trigger);
  const auto out = r.render(0.5, side.data());
  CHECK(amplitudeAt(std::vector<float>(out.begin() + 4800, out.end()), 330) > 0.05);
}

// ---- Global settings ----------------------------------------------------------------------

TEST("settings: unipolar CV outs; RESET in can recall the BUFFER") {
  Rig r;
  auto st = r.e.seq.state();
  st.held[0] = -5;
  r.e.seq.setState(st);
  r.e.params[Param::CvRange1] = 1;
  r.e.seq.setQuantMode(kScaleUnquantized);
  r.render(0.05);
  CHECK(r.e.jack(Out::Seq1Cv) < -4.9);
  r.e.unipolarCvOut = true;
  r.render(0.01);
  CHECK(std::abs(r.e.jack(Out::Seq1Cv)) < 0.01);

  r.e.seq.toggleCell(3);
  r.e.seq.saveBuffer();
  r.e.seq.toggleCell(3);
  r.e.resetRecallsBuffer = true;
  r.e.params[Param::Tempo] = (float)knobFor(8, 0.5, 30);
  r.e.patch.connect(Out::Clock, In::Reset);
  r.render(0.3);
  CHECK(r.e.seq.memory().cells[3].on);
}
