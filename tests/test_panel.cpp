#include "check.h"

#include "../engine/Panel.h"

#include <string>

using namespace catacomb;
using B = Button;

namespace {

struct Rig {
  Sequencer seq;
  Panel panel{seq};
  double t = 0;

  Rig(const char* seq1 = "00000000") {
    auto st = seq.state();
    for (int i = 0; i < kBits; i++) {
      st.mem.cells[i].on = seq1[i] == '1';
      st.mem.cells[i].volts = (float)i;
    }
    seq.setState(st);
  }

  void tap(B b) {
    panel.press(b, t);
    t += 0.05;
    panel.release(b, t);
    t += 0.05;
  }
  void down(B b) { panel.press(b, t += 0.01); }
  void up(B b) { panel.release(b, t += 0.01); }
  void wait(double s) {
    t += s;
    panel.tick(t);
  }
  std::string bits(int s = 0) const {
    std::string out;
    for (int i = 0; i < kBits; i++) out += seq.memory().cells[s * kBits + i].on ? '1' : '0';
    return out;
  }
  bool saw(PanelEvent::Type type, int value = 0) const {
    for (int i = 0; i < panel.numEvents(); i++)
      if (panel.event(i).type == type && panel.event(i).value == value) return true;
    return false;
  }
};

} // namespace

TEST("panel: BIT SHIFT alone rotates on release") {
  Rig r("10000000");
  r.down(B::Shift1);
  CHECK(r.bits() == "10000000"); // nothing yet: it might be a modifier
  r.up(B::Shift1);
  CHECK(r.bits() == "01000000");
}

TEST("panel: BIT SHIFT 1 + BIT FLIP 1 / LENGTH 1 steps the scale, without rotating or flipping") {
  Rig r("10000000");
  const int q = r.seq.quantMode();
  r.down(B::Shift1);
  r.tap(B::Flip1);
  r.tap(B::Flip1);
  r.tap(B::Length1);
  r.up(B::Shift1);
  CHECK(r.seq.quantMode() == q + 1);
  CHECK(r.bits() == "10000000");
  CHECK(r.seq.loopLength(0) == 8);
  CHECK(r.saw(PanelEvent::ShowQuantMode));
}

TEST("panel: BIT SHIFT 2 + BIT FLIP 2 / LENGTH 2 change the clock division") {
  Rig r;
  r.down(B::Shift2);
  r.tap(B::Flip2);
  CHECK(r.saw(PanelEvent::ClockDivision, +1));
  r.tap(B::Length2);
  CHECK(r.saw(PanelEvent::ClockDivision, -1));
  r.up(B::Shift2);
  CHECK(r.seq.loopLength(1) == 8);
}

TEST("panel: LENGTH shortens; LENGTH + RESET (either order) restores 8") {
  Rig r;
  r.tap(B::Length1);
  r.tap(B::Length1);
  CHECK(r.seq.loopLength(0) == 6);
  r.down(B::Length1);
  r.down(B::Reset);
  r.up(B::Reset);
  r.up(B::Length1);
  CHECK(r.seq.loopLength(0) == 8);
  r.tap(B::Length1);
  r.down(B::Reset);
  r.down(B::Length1);
  r.up(B::Length1);
  r.up(B::Reset);
  CHECK(r.seq.loopLength(0) == 8);
}

TEST("panel: BIT SHIFT + RESET undoes rotation") {
  Rig r("11010000");
  r.tap(B::Shift1);
  r.tap(B::Shift1);
  r.down(B::Shift1);
  r.tap(B::Reset);
  r.up(B::Shift1);
  CHECK(r.bits() == "11010000");
}

TEST("panel: BIT SHIFT + ADVANCE offsets the write head; + RESET returns it") {
  Rig r;
  r.down(B::Shift1);
  r.tap(B::Advance);
  r.tap(B::Advance);
  r.up(B::Shift1);
  CHECK(r.seq.writeCell(0) == 2 && r.seq.playCell(0) == 0);
  r.down(B::Shift1);
  r.down(B::Advance);
  r.tap(B::Reset);
  r.up(B::Advance);
  r.up(B::Shift1);
  CHECK(r.seq.writeCell(0) == 0);
}

TEST("panel: BIT FLIP flips now, and keeps flipping while held") {
  Rig r;
  r.seq.setRunning(true);
  r.down(B::Flip1);
  CHECK(r.bits() == "10000000");
  r.seq.clock(0);
  r.seq.clock(0);
  r.up(B::Flip1);
  r.seq.clock(0);
  CHECK(r.bits() == "11100000");
}

TEST("panel: ADVANCE only while stopped; RUN/STOP toggles; RESET goes to bit 1") {
  Rig r;
  r.tap(B::Advance);
  CHECK(r.seq.playStep(0) == 1);
  r.tap(B::RunStop);
  CHECK(r.seq.isRunning());
  r.tap(B::Advance);
  CHECK(r.seq.playStep(0) == 1);
  r.tap(B::Reset);
  CHECK(r.seq.playStep(0) == 0);
}

TEST("panel: BUFFER — hold 1 s saves, a tap recalls") {
  Rig r("10100000");
  r.down(B::Buffer);
  r.wait(0.5);
  CHECK(!r.seq.hasBuffer());
  r.wait(0.6);
  CHECK(r.seq.hasBuffer() && r.saw(PanelEvent::BufferSaved));
  r.up(B::Buffer); // releasing after a save doesn't recall
  CHECK(!r.saw(PanelEvent::BufferRecalled));
  r.tap(B::Flip1);
  CHECK(r.bits() == "00100000");
  r.tap(B::Buffer);
  CHECK(r.bits() == "10100000" && r.saw(PanelEvent::BufferRecalled));
}

TEST("panel: BUFFER + RESET held 1 s clears, without saving") {
  Rig r("11111111");
  r.down(B::Buffer);
  r.down(B::Reset);
  r.wait(1.1);
  r.up(B::Reset);
  r.up(B::Buffer);
  CHECK(r.bits() == "00000000");
  CHECK(!r.seq.hasBuffer() && r.saw(PanelEvent::Cleared));
}

TEST("panel: chained, BIT SHIFT 2 offsets head 2; + RESET resyncs") {
  Rig r;
  r.tap(B::Chain);
  CHECK(r.seq.chained());
  r.tap(B::Shift2);
  CHECK(r.seq.playStep(1) == 1 && r.seq.playStep(0) == 0);
  r.down(B::Shift2);
  r.tap(B::Reset);
  r.up(B::Shift2);
  CHECK(r.seq.playStep(1) == 0);
}

TEST("panel: TRIGGER asks for both envelopes") {
  Rig r;
  r.tap(B::Trigger);
  CHECK(r.saw(PanelEvent::ManualTrigger));
}
