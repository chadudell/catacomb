// Catacomb — the sequencer's buttons, with the hardware's combos (manual p. 37).
//
// The UI (or MIDI keyswitches) only reports raw presses and releases; this class
// decides what they mean. BIT SHIFT and BUFFER act on release, because they are also
// modifiers: BIT SHIFT 1 + BIT FLIP 1 must change the scale without rotating the
// pattern, and a held BUFFER saves instead of recalling.
#pragma once

#include "Sequencer.h"

namespace catacomb {

enum class Button {
  Buffer, Reset, Advance, Chain, RunStop, Trigger,
  Length1, Shift1, Flip1,
  Length2, Shift2, Flip2,
  Count
};

// What the panel tells the rest of the plugin: actions outside the sequencer
// (TRIGGER, clock division) and LED feedback for the UI.
struct PanelEvent {
  enum Type {
    ManualTrigger,   // fire EG1 + EG2
    ClockDivision,   // value = -1 / +1 (BIT SHIFT 2 + LENGTH 2 / BIT FLIP 2)
    ShowLength,      // value = seq; green LEDs show its length
    ShowQuantMode,   // the scale changed
    BufferSaved,     // green flash ×3
    BufferRecalled,
    Cleared,
  } type;
  int value = 0;
};

class Panel {
public:
  static constexpr double kHoldSeconds = 1.0;
  static constexpr int kMaxEvents = 32;

  explicit Panel(Sequencer& s) : seq(s) {}

  // `now` is a running time in seconds (the audio thread's sample clock).
  void press(Button b, double now);
  void release(Button b, double now);
  void tick(double now); // checks BUFFER holds; call once per block

  bool isHeld(Button b) const { return held[(int)b]; }

  // Drain events after each block.
  int numEvents() const { return eventCount; }
  const PanelEvent& event(int i) const { return events[i]; }
  void clearEvents() { eventCount = 0; }

private:
  void emit(PanelEvent::Type t, int value = 0);
  bool& h(Button b) { return held[(int)b]; }
  bool& used(Button b) { return consumed[(int)b]; }

  Sequencer& seq;
  bool held[(int)Button::Count]{};
  bool consumed[(int)Button::Count]{};
  double pressedAt[(int)Button::Count]{};
  bool bufferHoldDone = false;
  PanelEvent events[kMaxEvents]{};
  int eventCount = 0;
};

} // namespace catacomb
