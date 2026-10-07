// Catacomb — the patch bay (manual pp. 39–45): 19 inputs and 12 outputs (the MIDI
// jack is the host's MIDI). An unpatched input uses its normal (the hard-wired
// default); a patched one takes the sum of the outputs cabled to it.
#pragma once

#include <cstdint>

namespace catacomb {

enum class In {
  Vco1VOct, MvcoSync, Mvco1VOct,
  Blend, VcwIn, Fold, VcwVcaCv,
  VcfIn, Cutoff, VcfVcaCv,
  UMix1, UMix2, Eg2Trig,
  Clock1, BitFlip1, Clock2, BitFlip2,
  Trigger, Reset,
  Count
};

enum class Out {
  Vca, Mvco, Noise, Mixer, Eg1, Eg2, UMix12,
  Seq1Cv, Seq1Trig, Seq2Cv, Seq2Trig, Clock,
  Count
};

constexpr int kNumIns = (int)In::Count;
constexpr int kNumOuts = (int)Out::Count;

// Which outputs feed each input: bit n of cables[input] = output n. All zero = every
// input on its normal (the unpatched instrument).
struct Patch {
  uint16_t cables[kNumIns]{};
  bool patched(In i) const { return cables[(int)i] != 0; }
  void connect(Out o, In i) { cables[(int)i] = (uint16_t)(cables[(int)i] | (1u << (int)o)); }
  void disconnect(Out o, In i) { cables[(int)i] = (uint16_t)(cables[(int)i] & ~(1u << (int)o)); }
};

} // namespace catacomb
