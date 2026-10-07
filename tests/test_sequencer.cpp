#include "check.h"

#include "../engine/Quantizer.h"
#include "../engine/Sequencer.h"

#include <cstring>
#include <string>

using namespace catacomb;

namespace {

// Bits as a string ("10110000"); cell volts are set to distinct values (bit index + 1,
// negated for SEQ2) so tests can tell cells apart.
void setBits(Sequencer& s, int seq, const char* bits) {
  auto st = s.state();
  for (int i = 0; i < kBits; i++) {
    auto& c = st.mem.cells[seq * kBits + i];
    c.on = bits[i] == '1';
    c.volts = (float)((i + 1) * (seq == 0 ? 0.5 : -0.5));
  }
  s.setState(st);
}

std::string bitsOf(const Sequencer& s, int seq) {
  std::string out;
  for (int i = 0; i < kBits; i++) out += s.memory().cells[seq * kBits + i].on ? '1' : '0';
  return out;
}

float voltsAt(const Sequencer& s, int cell) { return s.memory().cells[cell].volts; }

// Clock head h `n` times and return the trigger pattern.
std::string run(Sequencer& s, int h, int n) {
  std::string out;
  for (int i = 0; i < n; i++) out += s.clock(h) ? 'x' : '.';
  return out;
}

Sequencer fresh(const char* seq1 = "00000000", const char* seq2 = "00000000") {
  Sequencer s;
  setBits(s, 0, seq1);
  setBits(s, 1, seq2);
  s.setRunning(true);
  return s;
}

} // namespace

// ---- Quantizer ---------------------------------------------------------------------------

TEST("quantizer: unquantized passes voltages through, ignoring the root") {
  CHECK_NEAR(quantize(1.234, kScaleUnquantized, 7), 1.234, 1e-12);
}

TEST("quantizer: chromatic rounds to the nearest semitone") {
  CHECK_NEAR(quantize(0.04 + 1.0 / 12, 1), 1.0 / 12, 1e-12);
  CHECK_NEAR(quantize(-0.04 - 3.0 / 12, 1), -3.0 / 12, 1e-12);
}

TEST("quantizer: major snaps to scale tones in every octave") {
  // C# (1) → C (0) or D (2)? tie → lower; 6 (F#) → 5 or 7, tie → lower (F).
  CHECK_NEAR(quantize(1.0 / 12, kScaleMajor) * 12, 0, 1e-9);
  CHECK_NEAR(quantize(6.0 / 12, kScaleMajor) * 12, 5, 1e-9);
  CHECK_NEAR(quantize(6.4 / 12, kScaleMajor) * 12, 7, 1e-9);
  CHECK_NEAR(quantize(-1.0 / 12, kScaleMajor) * 12, -1, 1e-9);  // B below
  CHECK_NEAR(quantize(-4.9, kScaleMajor) * 12, -58, 1e-9);  // -58.8 → D, 5 octaves down
}

TEST("quantizer: the root transposes scale and output") {
  // Root D (2): input 0 V is in D major → stays D, i.e. +2 semitones out.
  CHECK_NEAR(quantize(0.0, kScaleMajor, 2) * 12, 2, 1e-9);
  // 3 semitones above the root isn't in major → 2 or 4 above the root.
  const double out = quantize(3.0 / 12, kScaleMajor, 2) * 12 - 2; // degree above root
  CHECK(std::abs(out - 2) < 1e-9 || std::abs(out - 4) < 1e-9);
}

TEST("quantizer: every scale's output lands on its pitch classes") {
  Rng r(42);
  for (int sc = 1; sc < kNumScales; sc++)
    for (int i = 0; i < 2000; i++) {
      const double semis = quantize(r.uniform(-5, 5), sc) * 12;
      const int n = (int)std::lround(semis);
      CHECK_NEAR(semis, n, 1e-9);
      CHECK(kScales[sc].mask & (1u << (((n % 12) + 12) % 12)));
    }
}

// ---- Clocking ----------------------------------------------------------------------------

TEST("sequencer: a trigger on each on bit, looping at the length") {
  auto s = fresh("10110000");
  // Starts on bit 1; each clock moves to the next bit first.
  CHECK(run(s, 0, 16) == ".xx....x.xx....x");
}

TEST("sequencer: clocks are ignored while stopped") {
  auto s = fresh("11111111");
  s.setRunning(false);
  CHECK(run(s, 0, 4) == "....");
  CHECK(s.playStep(0) == 0);
}

TEST("sequencer: the two sequencers run independently") {
  auto s = fresh("10000000", "01000000");
  CHECK(run(s, 0, 8) == ".......x");
  CHECK(run(s, 1, 3) == "x..");
}

TEST("sequencer: LENGTH steps 8 → 1 → 8, and LENGTH + RESET restores 8") {
  auto s = fresh("11111111");
  for (int expect = 7; expect >= 1; expect--) {
    s.decrementLength(0);
    CHECK(s.loopLength(0) == expect);
  }
  s.decrementLength(0);
  CHECK(s.loopLength(0) == 8);
  s.decrementLength(0);
  s.decrementLength(0);
  s.resetLength(0);
  CHECK(s.loopLength(0) == 8);
}

TEST("sequencer: a shorter length loops early") {
  auto s = fresh("10000001");
  for (int i = 0; i < 5; i++) s.decrementLength(0); // length 3
  CHECK(run(s, 0, 6) == "..x..x");
}

// ---- Bit flips -----------------------------------------------------------------------------

TEST("sequencer: BIT FLIP toggles the bit under the write head; on rolls a new voltage") {
  auto s = fresh();
  s.flip(0);
  CHECK(bitsOf(s, 0) == "10000000");
  const float v1 = voltsAt(s, 0);
  CHECK(v1 >= -5 && v1 <= 5);
  s.flip(0);
  CHECK(bitsOf(s, 0) == "00000000");
  s.flip(0);
  CHECK(voltsAt(s, 0) != v1); // a fresh random value (manual p. 15)
}

TEST("sequencer: holding BIT FLIP inverts each bit the write head reaches") {
  auto s = fresh("11110000");
  s.setFlipHeld(0, true);
  run(s, 0, 8);
  s.setFlipHeld(0, false);
  CHECK(bitsOf(s, 0) == "00001111");
}

TEST("sequencer: the BIT FLIP jack flips while high, sampled on the clock") {
  auto s = fresh();
  s.setFlipGate(0, true);
  s.clock(0); // → bit 2
  s.setFlipGate(0, false);
  s.clock(0); // → bit 3
  CHECK(bitsOf(s, 0) == "01000000");
}

TEST("sequencer: the walkthrough rhythm (manual p. 14)") {
  // Clear, flip bits 1, 3, 5, 6 with ADVANCE while stopped, RESET, then run.
  auto s = fresh();
  s.setRunning(false);
  s.flip(0);                         // bit 1
  s.advance(); s.advance(); s.flip(0); // bit 3
  s.advance(); s.advance(); s.flip(0); // bit 5
  s.advance(); s.flip(0);              // bit 6
  s.reset();
  CHECK(bitsOf(s, 0) == "10101100");
  s.setRunning(true);
  CHECK(run(s, 0, 8) == ".x.xx..x");
}

// ---- BIT SHIFT -------------------------------------------------------------------------------

TEST("sequencer: BIT SHIFT rotates right, wrapping bit 8 to bit 1") {
  auto s = fresh("11000001");
  s.bitShift(0);
  CHECK(bitsOf(s, 0) == "11100000");
  CHECK(voltsAt(s, 0) == 4.0f); // bit 8's voltage came along
}

TEST("sequencer: BIT SHIFT only rotates within the current length") {
  auto s = fresh("10010011");
  for (int i = 0; i < 4; i++) s.decrementLength(0); // length 4: "1001" | "0011"
  s.bitShift(0);
  CHECK(bitsOf(s, 0) == "11000011");
}

TEST("sequencer: BIT SHIFT + RESET undoes all rotation, even across length changes") {
  auto s = fresh("10010110");
  s.bitShift(0);
  s.bitShift(0);
  s.decrementLength(0);
  s.decrementLength(0);
  s.bitShift(0);
  s.resetLength(0);
  s.bitShift(0);
  s.bitShiftReset(0);
  CHECK(bitsOf(s, 0) == "10010110");
  for (int i = 0; i < kBits; i++) CHECK(voltsAt(s, i) == (float)((i + 1) * 0.5));
}

// ---- Write head ------------------------------------------------------------------------------

TEST("sequencer: an offset write head flips ahead of the play head") {
  auto s = fresh();
  s.shiftWriteHead(0);
  s.shiftWriteHead(0);
  s.flip(0);
  CHECK(bitsOf(s, 0) == "00100000");
  CHECK(s.writeCell(0) == 2 && s.playCell(0) == 0);
}

TEST("sequencer: RESET keeps the write-head offset; BIT SHIFT + ADVANCE + RESET clears it") {
  auto s = fresh();
  s.shiftWriteHead(0);
  run(s, 0, 3);
  s.reset();
  CHECK(s.playCell(0) == 0 && s.writeCell(0) == 1);
  s.resetWriteHead(0);
  CHECK(s.writeCell(0) == 0);
}

TEST("sequencer: ADVANCE moves both heads only while stopped") {
  auto s = fresh();
  s.advance();
  CHECK(s.playStep(0) == 0);
  s.setRunning(false);
  s.advance();
  CHECK(s.playStep(0) == 1 && s.playStep(1) == 1);
}

// ---- CHAIN ------------------------------------------------------------------------------------

TEST("chain: one loop of SEQ1 then SEQ2 bits, both heads in phase") {
  auto s = fresh("10000000", "00000001");
  run(s, 0, 2);
  s.setChained(true);
  CHECK(s.loopLength(0) == 16 && s.loopLength(1) == 16);
  CHECK(s.playStep(1) == s.playStep(0));
  s.reset();
  // From step 1: SEQ2's bit 8 is step 16, then SEQ1's bit 1 is step 1 again.
  CHECK(run(s, 0, 16) == "..............xx");
}

TEST("chain: lengths combine (5 + 3 = 8 steps)") {
  auto s = fresh("00001000", "00100000");
  for (int i = 0; i < 3; i++) s.decrementLength(0);
  for (int i = 0; i < 5; i++) s.decrementLength(1);
  s.setChained(true);
  CHECK(s.loopLength(0) == 8);
  CHECK(run(s, 0, 8) == "...x..x."); // SEQ1 bit 5 = step 5, SEQ2 bit 3 = step 7
}

TEST("chain: BIT SHIFT 2 offsets the SEQ2 play head; + RESET resyncs") {
  auto s = fresh();
  s.setChained(true);
  s.bitShift(1);
  s.bitShift(1);
  CHECK(s.playStep(1) == 2 && s.playStep(0) == 0);
  CHECK(bitsOf(s, 1) == "00000000");
  s.bitShiftReset(1);
  CHECK(s.playStep(1) == 0);
}

TEST("chain: BIT SHIFT 1 rotates all 16 bits; + RESET restores them") {
  auto s = fresh("00000001", "10000000");
  s.setChained(true);
  s.bitShift(0);
  CHECK(bitsOf(s, 0) == "00000000" && bitsOf(s, 1) == "11000000");
  s.bitShift(0);
  s.bitShiftReset(0);
  CHECK(bitsOf(s, 0) == "00000001" && bitsOf(s, 1) == "10000000");
}

TEST("chain: unchaining puts each head back on its own sequencer") {
  auto s = fresh();
  s.setChained(true);
  run(s, 0, 10); // step 10 = SEQ2 bit 2
  run(s, 1, 3);  // step 3 = SEQ1 bit 3
  s.setChained(false);
  CHECK(s.playCell(0) == 0);         // was on SEQ2 → bit 1
  CHECK(s.playCell(1) == kBits + 0); // was on SEQ1 → bit 1
  run(s, 0, 1);
  CHECK(s.playCell(0) == 1);
}

// ---- CORRUPT ----------------------------------------------------------------------------------

TEST("corrupt: the manual's probability curve") {
  CHECK_NEAR(Sequencer::corruptVoltageChance(0), 0, 1e-12);
  CHECK_NEAR(Sequencer::corruptVoltageChance(0.5), 0.25, 1e-12);
  CHECK_NEAR(Sequencer::corruptVoltageChance(1), 0.5, 1e-12);
  CHECK_NEAR(Sequencer::corruptFlipChance(0.5), 0, 1e-12);
  CHECK_NEAR(Sequencer::corruptFlipChance(1), 0.5, 1e-12);
}

TEST("corrupt: fully CCW locks the pattern") {
  auto s = fresh("10110100", "01001011");
  const auto before = s.memory();
  for (int i = 0; i < 5000; i++) {
    s.clock(0);
    s.clock(1);
  }
  CHECK(std::memcmp(&before.cells, &s.memory().cells, sizeof before.cells) == 0);
}

TEST("corrupt: below noon changes voltages (≈25% at noon) but never bits") {
  auto s = fresh("11111111");
  s.corrupt[0] = 0.5;
  int changes = 0, passes = 0;
  for (int i = 0; i < 20000; i++) {
    s.clock(0);
    const int w = s.writeCell(0);
    static float last[kBits] = {0.5f, 1, 1.5f, 2, 2.5f, 3, 3.5f, 4};
    if (voltsAt(s, w) != last[w]) changes++;
    last[w] = voltsAt(s, w);
    passes++;
  }
  CHECK(bitsOf(s, 0) == "11111111");
  CHECK_NEAR((double)changes / passes, 0.25, 0.015);
}

TEST("corrupt: fully CW flips bits about half the time") {
  auto s = fresh("11111111");
  s.corrupt[0] = 1;
  int flips = 0;
  bool last[kBits];
  for (int i = 0; i < kBits; i++) last[i] = true;
  for (int i = 0; i < 20000; i++) {
    s.clock(0);
    const int w = s.writeCell(0);
    if (s.memory().cells[w].on != last[w]) flips++;
    last[w] = s.memory().cells[w].on;
  }
  CHECK_NEAR(flips / 20000.0, 0.5, 0.015);
}

// ---- CV ---------------------------------------------------------------------------------------

TEST("cv: scaled by CV RANGE, quantized, held through off bits") {
  auto s = fresh("10000000");
  s.setQuantMode(kScaleUnquantized);
  s.cvRange[0] = 0.5;
  run(s, 0, 8); // back on bit 1 (0.5 V)
  CHECK_NEAR(s.cv(0), 0.25, 1e-6);
  run(s, 0, 3); // off bits: still holds
  CHECK_NEAR(s.cv(0), 0.25, 1e-6);
  s.cvRange[0] = 0;
  CHECK_NEAR(s.cv(0), 0, 1e-12); // fully CCW: just the root (tune the VCO here)
}

TEST("cv: MIDI root transposes only when quantized") {
  auto s = fresh("10000000");
  s.cvRange[0] = 0;
  s.rootSemis = 5;
  s.setQuantMode(kScaleMajor);
  CHECK_NEAR(s.cv(0) * 12, 5, 1e-9);
  s.setQuantMode(kScaleUnquantized);
  CHECK_NEAR(s.cv(0), 0, 1e-12);
}

// ---- BUFFER / clear / quant mode ---------------------------------------------------------------

TEST("buffer: save, mutate, recall") {
  auto s = fresh("10100000", "00000011");
  s.decrementLength(1);
  s.shiftWriteHead(0);
  s.setQuantMode(12);
  s.saveBuffer();
  s.corrupt[0] = s.corrupt[1] = 1;
  for (int i = 0; i < 64; i++) {
    s.clock(0);
    s.clock(1);
  }
  s.resetLength(1);
  s.setQuantMode(3);
  s.setChained(true);
  s.recallBuffer();
  CHECK(bitsOf(s, 0) == "10100000" && bitsOf(s, 1) == "00000011");
  CHECK(voltsAt(s, 2) == 1.5f);
  CHECK(s.loopLength(1) == 7 && s.quantMode() == 12 && !s.chained());
  CHECK(s.memory().writeOffset[0] == 1);
}

TEST("clear: all bits off, quantizer and write offsets reset, lengths kept") {
  auto s = fresh("11111111", "11111111");
  s.decrementLength(0);
  s.shiftWriteHead(1);
  s.clear();
  CHECK(bitsOf(s, 0) == "00000000" && bitsOf(s, 1) == "00000000");
  CHECK(s.quantMode() == kScaleUnquantized && s.memory().writeOffset[1] == 0);
  CHECK(s.loopLength(0) == 7);
}

TEST("quant mode steps wrap 1 ↔ 16") {
  Sequencer s;
  s.setQuantMode(0);
  s.stepQuantMode(-1);
  CHECK(s.quantMode() == 15);
  s.stepQuantMode(+1);
  CHECK(s.quantMode() == 0);
}

// ---- Determinism ---------------------------------------------------------------------------------

TEST("state: a restored sequencer mutates identically (project reload / bounce)") {
  Sequencer a;
  a.factoryPattern();
  a.setRunning(true);
  a.corrupt[0] = 0.8;
  a.corrupt[1] = 0.6;
  for (int i = 0; i < 100; i++) a.clock(i % 2);
  Sequencer b;
  b.setState(a.state());
  b.corrupt[0] = 0.8;
  b.corrupt[1] = 0.6;
  for (int i = 0; i < 1000; i++) {
    const int h = (i * 7) % 3 == 0 ? 1 : 0;
    CHECK(a.clock(h) == b.clock(h));
  }
  CHECK(std::memcmp(&a.memory().cells, &b.memory().cells, sizeof(SeqMemory::cells)) == 0);
}

TEST("toggleCell: clicking an LED flips that bit; on rolls a new voltage") {
  auto s = fresh("00000000", "00000000");
  s.toggleCell(10);
  CHECK(bitsOf(s, 1) == "00100000");
  CHECK(voltsAt(s, 10) != -1.5f);
  s.toggleCell(10);
  s.toggleCell(99); // ignored
  CHECK(bitsOf(s, 1) == "00000000");
}
