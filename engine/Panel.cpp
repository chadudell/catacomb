#include "Panel.h"

#include <algorithm>

namespace catacomb {

namespace {

Button shiftOf(int s) { return s == 0 ? Button::Shift1 : Button::Shift2; }

} // namespace

void Panel::emit(PanelEvent::Type t, int value) {
  if (eventCount < kMaxEvents) events[eventCount++] = {t, value};
}

void Panel::press(Button b, double now) {
  if (held[(int)b]) return; // repeated press without a release
  h(b) = true;
  used(b) = false;
  pressedAt[(int)b] = now;

  // Which BIT SHIFT (if any) is held, as a modifier.
  const int shift = h(Button::Shift1) ? 0 : h(Button::Shift2) ? 1 : -1;

  switch (b) {
    case Button::Flip1:
    case Button::Flip2: {
      const int s = b == Button::Flip1 ? 0 : 1;
      if (h(shiftOf(s))) {
        // BIT SHIFT 1 + BIT FLIP 1 → next scale; BIT SHIFT 2 + BIT FLIP 2 → clock div +.
        used(shiftOf(s)) = used(b) = true;
        if (s == 0) {
          seq.stepQuantMode(+1);
          emit(PanelEvent::ShowQuantMode);
        } else {
          emit(PanelEvent::ClockDivision, +1);
        }
      } else {
        seq.flip(s);
        seq.setFlipHeld(s, true); // held: flips each step the write head reaches
      }
      break;
    }

    case Button::Length1:
    case Button::Length2: {
      const int s = b == Button::Length1 ? 0 : 1;
      if (h(shiftOf(s))) {
        used(shiftOf(s)) = used(b) = true;
        if (s == 0) {
          seq.stepQuantMode(-1);
          emit(PanelEvent::ShowQuantMode);
        } else {
          emit(PanelEvent::ClockDivision, -1);
        }
      } else if (h(Button::Reset)) {
        used(Button::Reset) = true;
        seq.resetLength(s);
        emit(PanelEvent::ShowLength, s);
      } else {
        seq.decrementLength(s);
        emit(PanelEvent::ShowLength, s);
      }
      break;
    }

    case Button::Reset:
      if (h(Button::Buffer)) {
        // BUFFER + RESET held for a second clears (see tick()).
        used(Button::Buffer) = true;
      } else if (shift >= 0 && h(Button::Advance)) {
        used(shiftOf(shift)) = used(Button::Advance) = true;
        seq.resetWriteHead(shift);
      } else if (shift >= 0) {
        used(shiftOf(shift)) = true;
        seq.bitShiftReset(shift);
      } else if (h(Button::Length1) || h(Button::Length2)) {
        // LENGTH pressed first already shortened it by one; RESET puts it at 8.
        const int s = h(Button::Length1) ? 0 : 1;
        seq.resetLength(s);
        emit(PanelEvent::ShowLength, s);
      } else {
        seq.reset();
      }
      break;

    case Button::Advance:
      if (shift >= 0) {
        used(shiftOf(shift)) = true;
        seq.shiftWriteHead(shift);
      } else {
        seq.advance(); // ignored while running
      }
      break;

    case Button::Buffer:
      bufferHoldDone = false;
      break;

    case Button::Chain:
      seq.setChained(!seq.chained());
      break;

    case Button::RunStop:
      seq.setRunning(!seq.isRunning());
      break;

    case Button::Trigger:
      emit(PanelEvent::ManualTrigger);
      break;

    case Button::Shift1:
    case Button::Shift2:
    case Button::Count:
      break; // BIT SHIFT acts on release
  }
}

void Panel::release(Button b, double now) {
  if (!held[(int)b]) return;
  tick(now);
  h(b) = false;

  switch (b) {
    case Button::Shift1:
    case Button::Shift2:
      if (!used(b)) seq.bitShift(b == Button::Shift1 ? 0 : 1);
      break;
    case Button::Flip1:
      seq.setFlipHeld(0, false);
      break;
    case Button::Flip2:
      seq.setFlipHeld(1, false);
      break;
    case Button::Buffer:
      if (!used(b) && !bufferHoldDone && seq.hasBuffer()) {
        seq.recallBuffer();
        emit(PanelEvent::BufferRecalled);
      }
      break;
    case Button::Reset:
    case Button::Advance:
    case Button::Chain:
    case Button::RunStop:
    case Button::Trigger:
    case Button::Length1:
    case Button::Length2:
    case Button::Count:
      break; // these act on press
  }
}

void Panel::tick(double now) {
  if (!h(Button::Buffer) || bufferHoldDone) return;
  const bool clearing = h(Button::Reset);
  const double since = clearing ? std::max(pressedAt[(int)Button::Buffer], pressedAt[(int)Button::Reset])
                                : pressedAt[(int)Button::Buffer];
  if (now - since < kHoldSeconds) return;
  bufferHoldDone = true;
  if (clearing) {
    seq.clear();
    emit(PanelEvent::Cleared);
  } else if (!used(Button::Buffer)) {
    seq.saveBuffer();
    emit(PanelEvent::BufferSaved);
  }
}

} // namespace catacomb
