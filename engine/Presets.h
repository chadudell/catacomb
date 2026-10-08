// Catacomb — factory presets. Each is a full starting point: knobs (anything not listed
// stays at its default), the sequencer's bits / scale / lengths, cables and the step
// division. The seed fixes the pitches the bits hold and how CORRUPT will mutate them,
// so a preset always starts the same way.
#pragma once

#include "Jacks.h"
#include "Params.h"
#include "Sequencer.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace catacomb {

struct Preset {
  const char* name;
  std::vector<std::pair<const char*, float>> params; // engine knob ids (Params.h)
  const char* bits1 = "00000000";
  const char* bits2 = "00000000";
  int scale = kScaleMajor; // 0-based (0 = Unquantized)
  const char* cables = "";  // StateText.h format: "eg2>bitFlip1,…"
  int clockDiv = 4;         // Divisions.h index (4 = 1/16)
  int length1 = 8, length2 = 8;
  uint32_t seed = 1;
};

const std::vector<Preset>& factoryPresets();

// The knobs a preset sets, over the defaults.
Params presetParams(const Preset& p);
// The sequencer a preset starts with (its random generator seeded from the preset).
// Running state and position come from `base`.
Sequencer::State presetSequencer(const Preset& p, const Sequencer::State& base);

} // namespace catacomb
