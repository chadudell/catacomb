#include "check.h"

#include "../engine/Engine.h"
#include "../engine/StateText.h"

#include <cstring>
#include <vector>

using namespace catacomb;

// ---- State text --------------------------------------------------------------------------

TEST("state text: round trip, then mutates identically") {
  Sequencer a;
  a.factoryPattern();
  a.setRunning(true);
  a.corrupt[0] = 0.9;
  for (int i = 0; i < 37; i++) a.clock(0);
  a.decrementLength(1);
  a.shiftWriteHead(0);
  a.bitShift(1);
  a.saveBuffer();
  a.setChained(true);
  a.rootSemis = -3;

  Sequencer::State decoded{};
  CHECK(decodeSequencer(encodeSequencer(a.state()), decoded));
  Sequencer b;
  b.setState(decoded);
  CHECK(encodeSequencer(a.state()) == encodeSequencer(b.state()));

  b.corrupt[0] = a.corrupt[0];
  for (int i = 0; i < 500; i++) CHECK(a.clock(0) == b.clock(0));
  CHECK(encodeSequencer(a.state()) == encodeSequencer(b.state()));
}

TEST("state text: rejects anything that isn't ours, tolerates missing fields") {
  Sequencer::State s = Sequencer().state();
  CHECK(!decodeSequencer("", s));
  CHECK(!decodeSequencer("hello=1,2,3\n", s));
  CHECK(decodeSequencer("catacomb-seq=1\nmem.quant=7\nfuture.field=1,2\n", s));
  CHECK(s.mem.quantMode == 7);
}

// ---- Clock --------------------------------------------------------------------------------

namespace {

// Samples until SEQ1's clock input sees its first step (seq running, every bit on).
int firstStep(Engine& e, int limit) {
  float buf[1];
  const int start = e.seq.playStep(0);
  for (int i = 0; i < limit; i++) {
    e.process(buf, 1);
    if (e.seq.playStep(0) != start) return i;
  }
  return -1;
}

void start(Engine& e) {
  e.prepare(48000);
  e.seq.setRunning(true);
}

} // namespace

TEST("clock: locked to a host grid, a step on the boundary fires at once") {
  Engine e;
  start(e);
  e.setClockRates(8, 0); // 8 steps a second
  e.setClockPhase(0, 0);
  e.armClocks(true);
  CHECK(firstStep(e, 48000) == 0);
}

TEST("clock: mid-step, the next step lands exactly where the grid says") {
  Engine e;
  start(e);
  e.setClockRates(8, 0);
  e.setClockPhase(0, 0.25); // a quarter through a 6000-sample step
  e.armClocks(false);
  const int at = firstStep(e, 48000);
  CHECK(at >= 4499 && at <= 4500);
}

TEST("clock: CLOCK 2 can run at its own rate") {
  Engine e;
  start(e);
  e.setClockRates(8, 4); // SEQ2 at half speed
  float buf[1];
  for (int i = 0; i < 47000; i++) e.process(buf, 1); // just short of the 1 s boundary
  // Steps at 0, 6000, …, 42000 for SEQ1 (8: back to bit 1); 0 … 36000 for SEQ2 (4).
  CHECK(e.seq.playStep(0) == 0);
  CHECK(e.seq.playStep(1) == 4);
}

TEST("clock: setPlayStep puts a head on a step") {
  Sequencer s;
  s.setPlayStep(0, 13);
  CHECK(s.playStep(0) == 5);
  s.setChained(true);
  s.setPlayStep(1, 13);
  CHECK(s.playStep(1) == 13);
}

TEST("resetVoice: the same notes render the same, whatever played before") {
  auto play = [](bool warmUp) {
    Engine e;
    e.params[Param::NoiseLvl] = 0.3f;
    e.params[Param::MvcoLvl] = 0.4f;
    e.prepare(48000); // after setting the knobs, so neither run is still smoothing
    std::vector<float> out(24000);
    if (warmUp) {
      e.press(Button::Trigger), e.release(Button::Trigger);
      e.process(out.data(), 24000);
    }
    e.resetVoice();
    e.press(Button::Trigger), e.release(Button::Trigger);
    e.process(out.data(), 24000);
    return out;
  };
  CHECK(play(true) == play(false));
}
