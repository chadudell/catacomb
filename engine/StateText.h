// Catacomb — the sequencer's state as text, for saving inside a Logic project.
//
// One "key=v,v,v" line per field. Voltages are stored as integer microvolts so the
// text never depends on the host's locale (a decimal comma would break strtod).
// Unknown keys are ignored and missing ones keep their defaults, so later versions
// can add fields.
#pragma once

#include "Jacks.h"
#include "Sequencer.h"

#include <string>
#include <vector>

namespace catacomb {

std::string encodeSequencer(const Sequencer::State& s);
// Returns false (leaving `out` untouched) if the text isn't a Catacomb sequencer state.
bool decodeSequencer(const std::string& text, Sequencer::State& out);

// Patch cables as text: "mvco>vcwIn,eg2>bitFlip1" (jack names from Jacks.h). The order is
// kept (the UI colours cables by it); unknown jacks and duplicates are dropped.
struct Cable {
  Out out;
  In in;
};
std::string encodeCables(const std::vector<Cable>& cables);
std::vector<Cable> decodeCables(const std::string& text);
Patch patchFrom(const std::vector<Cable>& cables);

} // namespace catacomb
