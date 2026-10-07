// Catacomb — the dual generative sequencer (manual pp. 11–18, 35–38).
//
// Storage is 16 cells: 0–7 belong to SEQ1, 8–15 to SEQ2. Each of the two heads
// (head 0 = SEQ1, head 1 = SEQ2) walks a loop of cells:
//
//   unchained   head 0: cells 0 … L1-1          head 1: cells 8 … 8+L2-1
//   chained     both:   cells 0 … L1-1, 8 … 8+L2-1   (one loop of L1+L2 ≤ 16 steps)
//
// A head has a play position and a write offset; its write head sits `offset` steps
// ahead of the play head on the same loop. Bit flips and CORRUPT act at the write
// head, triggers and CV come from the play head.
//
// This class is pure logic with no notion of samples: the caller clocks a head on
// each rising clock edge and reads triggers/CV back. No allocation; real-time safe.
#pragma once

#include "Quantizer.h"
#include "Rng.h"

#include <cstdint>

namespace catacomb {

constexpr int kBits = 8;
constexpr int kCells = 16;

struct Cell {
  bool on = false;
  float volts = 0; // the random value stored when the bit was flipped on, ±5 V
};

// Everything BUFFER saves (manual p. 37): bits, their voltages, quant mode, write
// offsets, lengths and CHAIN. `origin` remembers where each cell's contents started,
// so BIT SHIFT + RESET can undo rotation.
struct SeqMemory {
  Cell cells[kCells];
  uint8_t origin[kCells];
  int length[2] = {kBits, kBits};
  int writeOffset[2] = {0, 0};
  int quantMode = kScaleMajor;
  bool chained = false;
};

class Sequencer {
public:
  Sequencer();

  // Fills both sequencers with a fresh random pattern, like a new unit out of the box.
  void factoryPattern();

  // ---- Clocking ----------------------------------------------------------------------
  // Advance head `h` one step (a rising edge at its CLOCK). Applies BIT FLIP and CORRUPT
  // at the new write position, then returns true if the play head landed on an on bit
  // (a trigger at SEQn TRIG). Ignored while stopped.
  bool clock(int h);

  // ---- Panel actions ---------------------------------------------------------------
  void setRunning(bool r) { running = r; }
  bool isRunning() const { return running; }
  void reset();                 // both play heads → bit 1 (write offsets kept)
  void setPlayStep(int h, int step); // put head h on a step (host transport alignment)
  void advance();               // both heads +1, only while stopped; no trigger
  void flip(int seq);           // BIT FLIP pressed: flip the bit at the write head now
  void toggleCell(int cell);    // flip any bit directly (clicking an LED in the UI)
  void setFlipHeld(int seq, bool isHeld) { flipHeld[seq] = isHeld; }
  void setFlipGate(int seq, bool high) { flipGate[seq] = high; } // BIT FLIP n jack, sampled per clock
  void decrementLength(int seq);// LENGTH: 8 → 7 → … → 1 → 8
  void resetLength(int seq);    // LENGTH + RESET
  void bitShift(int seq);       // BIT SHIFT (chained: 1 rotates all, 2 offsets head 2)
  void bitShiftReset(int seq);  // BIT SHIFT + RESET (chained 2: resync head 2)
  void shiftWriteHead(int seq); // BIT SHIFT + ADVANCE
  void resetWriteHead(int seq); // BIT SHIFT + ADVANCE + RESET
  void setChained(bool c);
  void stepQuantMode(int delta);// BIT SHIFT 1 + BIT FLIP 1 / LENGTH 1 (wraps 1…16)
  void setQuantMode(int mode);
  void saveBuffer();            // hold BUFFER
  void recallBuffer();          // tap BUFFER
  void clear();                 // hold BUFFER + RESET: all bits off, quant 1, offsets 0
  void reroll();                // new voltages for every on bit (rhythm kept)

  // ---- Knobs -------------------------------------------------------------------------
  double corrupt[2] = {0, 0};   // 0…1, noon = 0.5
  double cvRange[2] = {0, 0};   // 0…1 attenuation before the quantizer
  int rootSemis = 0;            // MIDI note transposition (only while quantized)

  // Corrupt's two probabilities at knob position c (manual p. 16).
  static double corruptVoltageChance(double c);
  static double corruptFlipChance(double c);

  // ---- Outputs -----------------------------------------------------------------------
  // SEQn CV: the play head's voltage, scaled by CV RANGE and quantized. The value is
  // held from the last on bit the play head passed, so a decaying note doesn't jump.
  double cv(int h) const;
  double rawHeldVolts(int h) const { return held[h]; }
  int loopLength(int h) const;
  int playCell(int h) const { return cellAt(h, pos[h]); }
  int writeCell(int h) const { return cellAt(h, wrap(pos[h] + mem.writeOffset[h], loopLength(h))); }
  int playStep(int h) const { return pos[h]; }
  const SeqMemory& memory() const { return mem; }
  const SeqMemory& buffer() const { return saved; }
  bool hasBuffer() const { return bufferValid; }
  bool chained() const { return mem.chained; }
  int quantMode() const { return mem.quantMode; }

  // ---- Whole state, for saving with the project --------------------------------------
  struct State {
    SeqMemory mem, saved;
    bool bufferValid;
    int pos[2];
    float held[2];
    bool running;
    int rootSemis;
    uint32_t rng[4];
  };
  State state() const;
  void setState(const State& s);

  Rng rng;

private:
  int cellAt(int h, int step) const;
  static int wrap(int i, int n) { return ((i % n) + n) % n; }
  void flipCell(int cell);
  void rotate(int firstStep, int head, int count); // rotate `count` steps of head's loop right by one
  void restoreOrigins(int lo, int hi);             // undo rotation within cells [lo, hi)
  void clampHeads();
  void refreshHeld(int h);
  float randomVolts() { return (float)rng.uniform(-5.0, 5.0); }

  SeqMemory mem, saved;
  bool bufferValid = false;
  int pos[2] = {0, 0};
  float held[2] = {0, 0};
  bool running = false;
  bool flipHeld[2] = {false, false};
  bool flipGate[2] = {false, false};
};

} // namespace catacomb
