// Catacomb — the sequencer's state as text, for saving inside a Logic project.
//
// One "key=v,v,v" line per field. Voltages are stored as integer microvolts so the
// text never depends on the host's locale (a decimal comma would break strtod).
// Unknown keys are ignored and missing ones keep their defaults, so later versions
// can add fields.
#pragma once

#include "Sequencer.h"

#include <string>

namespace catacomb {

std::string encodeSequencer(const Sequencer::State& s);
// Returns false (leaving `out` untouched) if the text isn't a Catacomb sequencer state.
bool decodeSequencer(const std::string& text, Sequencer::State& out);

} // namespace catacomb
