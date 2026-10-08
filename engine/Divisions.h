// Catacomb — the step lengths the clock can lock to (16, like the hardware's MIDI clock
// divider: BIT SHIFT 2 + BIT FLIP 2 / LENGTH 2).
#pragma once

namespace catacomb {

struct Division {
  const char* name;
  double beats; // step length in quarter notes
};

constexpr Division kDivisions[16] = {
    {"1/64", 1.0 / 16}, {"1/32T", 1.0 / 12}, {"1/32", 1.0 / 8},  {"1/16T", 1.0 / 6},
    {"1/16", 1.0 / 4},  {"1/8T", 1.0 / 3},   {"1/16.", 3.0 / 8}, {"1/8", 1.0 / 2},
    {"1/4T", 2.0 / 3},  {"1/8.", 3.0 / 4},   {"1/4", 1.0},       {"1/2T", 4.0 / 3},
    {"1/4.", 1.5},      {"1/2", 2.0},        {"1/1", 4.0},       {"2/1", 8.0},
};
constexpr int kDefaultDivision = 4; // 1/16

} // namespace catacomb
