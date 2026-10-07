#include "Sequencer.h"

#include <algorithm>
#include <cstring>

namespace catacomb {

Sequencer::Sequencer() {
  for (int c = 0; c < kCells; c++) mem.origin[c] = (uint8_t)c;
  saved = mem;
}

void Sequencer::factoryPattern() {
  // Random, but never nearly empty or full: 3–6 bits on in each sequencer.
  for (int s = 0; s < 2; s++) {
    int count;
    do {
      count = 0;
      for (int b = 0; b < kBits; b++) count += (mem.cells[s * kBits + b].on = rng.chance(0.5));
    } while (count < 3 || count > 6);
  }
  for (int c = 0; c < kCells; c++) {
    mem.cells[c].volts = randomVolts();
    mem.origin[c] = (uint8_t)c;
  }
  mem.quantMode = kScaleMajor;
  refreshHeld(0);
  refreshHeld(1);
}

// ---- Loop geometry -------------------------------------------------------------------

int Sequencer::loopLength(int h) const {
  return mem.chained ? mem.length[0] + mem.length[1] : mem.length[h];
}

int Sequencer::cellAt(int h, int step) const {
  if (mem.chained) return step < mem.length[0] ? step : kBits + (step - mem.length[0]);
  return h == 0 ? step : kBits + step;
}

void Sequencer::clampHeads() {
  for (int h = 0; h < 2; h++) pos[h] = wrap(pos[h], loopLength(h));
}

void Sequencer::refreshHeld(int h) {
  const Cell& c = mem.cells[playCell(h)];
  if (c.on) held[h] = c.volts;
}

// ---- Corrupt (manual p. 16) ------------------------------------------------------------
// Fully CCW → nothing. Up to noon, only the stored voltage of an on bit may change
// (0 → 25 %). From noon to fully CW the voltage chance rises 25 → 50 % and the bit
// itself may also flip (0 → 50 %).

double Sequencer::corruptVoltageChance(double c) {
  c = std::clamp(c, 0.0, 1.0);
  return c <= 0.5 ? 0.5 * c : 0.25 + 0.5 * (c - 0.5);
}

double Sequencer::corruptFlipChance(double c) {
  c = std::clamp(c, 0.0, 1.0);
  return c <= 0.5 ? 0.0 : c - 0.5;
}

// ---- Clock -----------------------------------------------------------------------------

bool Sequencer::clock(int h) {
  if (!running) return false;
  pos[h] = wrap(pos[h] + 1, loopLength(h));

  // The write head passes a bit: BIT FLIP (button held, or the jack high) OR corrupt
  // XORs it; otherwise corrupt may give an on bit a new voltage.
  const int w = writeCell(h);
  const double pFlip = corruptFlipChance(corrupt[h]);
  const bool corruptFlip = pFlip > 0 && rng.chance(pFlip);
  if (flipHeld[h] || flipGate[h] || corruptFlip) {
    flipCell(w);
  } else if (mem.cells[w].on) {
    const double pVolts = corruptVoltageChance(corrupt[h]);
    if (pVolts > 0 && rng.chance(pVolts)) mem.cells[w].volts = randomVolts();
  }

  // Chained, both heads share cells, so the other head may be sitting on this one.
  refreshHeld(0);
  refreshHeld(1);
  return mem.cells[playCell(h)].on;
}

void Sequencer::flipCell(int cell) {
  Cell& c = mem.cells[cell];
  c.on = !c.on;
  if (c.on) c.volts = randomVolts(); // every flip on rolls a fresh voltage
}

// ---- Panel actions ---------------------------------------------------------------------

void Sequencer::reset() {
  pos[0] = pos[1] = 0;
  refreshHeld(0);
  refreshHeld(1);
}

void Sequencer::advance() {
  if (running) return;
  for (int h = 0; h < 2; h++) pos[h] = wrap(pos[h] + 1, loopLength(h));
  refreshHeld(0);
  refreshHeld(1);
}

void Sequencer::flip(int seq) {
  flipCell(writeCell(seq));
  refreshHeld(0);
  refreshHeld(1);
}

void Sequencer::decrementLength(int seq) {
  mem.length[seq] = mem.length[seq] == 1 ? kBits : mem.length[seq] - 1;
  clampHeads();
  refreshHeld(0);
  refreshHeld(1);
}

void Sequencer::resetLength(int seq) {
  mem.length[seq] = kBits;
  clampHeads();
}

void Sequencer::rotate(int firstStep, int h, int count) {
  if (count < 2) return;
  const int last = cellAt(h, firstStep + count - 1);
  const Cell carry = mem.cells[last];
  const uint8_t carryOrigin = mem.origin[last];
  for (int s = firstStep + count - 1; s > firstStep; s--) {
    const int to = cellAt(h, s), from = cellAt(h, s - 1);
    mem.cells[to] = mem.cells[from];
    mem.origin[to] = mem.origin[from];
  }
  const int first = cellAt(h, firstStep);
  mem.cells[first] = carry;
  mem.origin[first] = carryOrigin;
}

void Sequencer::bitShift(int seq) {
  if (mem.chained && seq == 1) {
    // Chained: BIT SHIFT 2 moves the SEQ2 play head, offsetting it from SEQ1's.
    pos[1] = wrap(pos[1] + 1, loopLength(1));
  } else {
    rotate(0, seq, loopLength(seq)); // chained, head 0's loop is all 16 steps
  }
  refreshHeld(0);
  refreshHeld(1);
}

void Sequencer::restoreOrigins(int lo, int hi) {
  // If chained rotation carried contents across the SEQ1/SEQ2 boundary, the range
  // alone can't be put back; restore all 16 cells instead.
  for (int c = lo; c < hi; c++)
    if (mem.origin[c] < lo || mem.origin[c] >= hi) {
      lo = 0;
      hi = kCells;
      break;
    }
  Cell cells[kCells];
  std::memcpy(cells, mem.cells, sizeof cells);
  for (int c = lo; c < hi; c++) mem.cells[mem.origin[c]] = cells[c];
  for (int c = lo; c < hi; c++) mem.origin[c] = (uint8_t)c;
}

void Sequencer::bitShiftReset(int seq) {
  if (mem.chained && seq == 1) {
    pos[1] = pos[0];
  } else if (mem.chained) {
    restoreOrigins(0, kCells);
  } else {
    restoreOrigins(seq * kBits, seq * kBits + kBits);
  }
  refreshHeld(0);
  refreshHeld(1);
}

void Sequencer::shiftWriteHead(int seq) { mem.writeOffset[seq] = (mem.writeOffset[seq] + 1) % kCells; }

void Sequencer::resetWriteHead(int seq) { mem.writeOffset[seq] = 0; }

void Sequencer::setChained(bool c) {
  if (c == mem.chained) return;
  if (c) {
    mem.chained = true;
    pos[1] = pos[0]; // the play heads start in phase (manual p. 18)
  } else {
    const int cell0 = playCell(0), cell1 = playCell(1);
    mem.chained = false;
    pos[0] = cell0 < kBits ? cell0 : 0;
    pos[1] = cell1 >= kBits ? cell1 - kBits : 0;
    clampHeads();
  }
  refreshHeld(0);
  refreshHeld(1);
}

void Sequencer::stepQuantMode(int delta) { mem.quantMode = wrap(mem.quantMode + delta, kNumScales); }

void Sequencer::setQuantMode(int mode) { mem.quantMode = std::clamp(mode, 0, kNumScales - 1); }

void Sequencer::saveBuffer() {
  saved = mem;
  bufferValid = true;
}

void Sequencer::recallBuffer() {
  if (!bufferValid) return;
  mem = saved;
  clampHeads();
  refreshHeld(0);
  refreshHeld(1);
}

void Sequencer::clear() {
  for (int c = 0; c < kCells; c++) {
    mem.cells[c] = Cell{};
    mem.origin[c] = (uint8_t)c;
  }
  mem.writeOffset[0] = mem.writeOffset[1] = 0;
  mem.quantMode = kScaleUnquantized;
}

void Sequencer::reroll() {
  for (auto& c : mem.cells)
    if (c.on) c.volts = randomVolts();
  refreshHeld(0);
  refreshHeld(1);
}

// ---- Outputs / state -------------------------------------------------------------------

double Sequencer::cv(int h) const { return quantize(cvRange[h] * held[h], mem.quantMode, rootSemis); }

Sequencer::State Sequencer::state() const {
  State s{};
  s.mem = mem;
  s.saved = saved;
  s.bufferValid = bufferValid;
  s.pos[0] = pos[0];
  s.pos[1] = pos[1];
  s.held[0] = held[0];
  s.held[1] = held[1];
  s.running = running;
  std::memcpy(s.rng, rng.s, sizeof s.rng);
  return s;
}

void Sequencer::setState(const State& s) {
  mem = s.mem;
  saved = s.saved;
  bufferValid = s.bufferValid;
  for (int i = 0; i < 2; i++) {
    mem.length[i] = std::clamp(mem.length[i], 1, kBits);
    saved.length[i] = std::clamp(saved.length[i], 1, kBits);
    mem.writeOffset[i] = std::clamp(mem.writeOffset[i], 0, kCells - 1);
    pos[i] = s.pos[i];
    held[i] = s.held[i];
  }
  setQuantMode(mem.quantMode);
  // A damaged state mustn't break rotation undo: origins must be a permutation.
  for (SeqMemory* m : {&mem, &saved}) {
    unsigned seen = 0;
    for (uint8_t o : m->origin) seen |= o < kCells ? 1u << o : 0u;
    if (seen != 0xffffu)
      for (int c = 0; c < kCells; c++) m->origin[c] = (uint8_t)c;
  }
  running = s.running;
  std::memcpy(rng.s, s.rng, sizeof s.rng);
  clampHeads();
}

} // namespace catacomb
