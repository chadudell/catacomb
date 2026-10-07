// Catacomb — the patch bay (manual pp. 39–45): 19 inputs and 12 outputs, plus one of our
// own, SIDECHAIN (the plugin's audio input). The hardware's MIDI jack is the host's MIDI.
// An unpatched input uses its normal (the hard-wired default); a patched one takes the
// sum of the outputs cabled to it.
#pragma once

#include <cstdint>
#include <cstring>

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
  Sidechain,
  Count
};

constexpr int kNumIns = (int)In::Count;
constexpr int kNumOuts = (int)Out::Count;

// Stable names: saved in projects and used by the UI.
constexpr const char* kInNames[kNumIns] = {
    "vco1voct", "mvcoSync", "mvco1voct", "blend", "vcwIn", "fold", "vcwVcaCv", "vcfIn", "cutoff", "vcfVcaCv",
    "umix1", "umix2", "eg2Trig", "clock1", "bitFlip1", "clock2", "bitFlip2", "trigger", "reset",
};
constexpr const char* kOutNames[kNumOuts] = {
    "vca", "mvco", "noise", "mixer", "eg1", "eg2", "umix12", "seq1Cv", "seq1Trig", "seq2Cv", "seq2Trig", "clock",
    "sidechain",
};

inline int inByName(const char* name) {
  for (int i = 0; i < kNumIns; i++)
    if (std::strcmp(name, kInNames[i]) == 0) return i;
  return -1;
}
inline int outByName(const char* name) {
  for (int i = 0; i < kNumOuts; i++)
    if (std::strcmp(name, kOutNames[i]) == 0) return i;
  return -1;
}

// Which outputs feed each input: bit n of cables[input] = output n. All zero = every
// input on its normal (the unpatched instrument).
struct Patch {
  uint16_t cables[kNumIns]{};
  bool patched(In i) const { return cables[(int)i] != 0; }
  void connect(Out o, In i) { cables[(int)i] = (uint16_t)(cables[(int)i] | (1u << (int)o)); }
  void disconnect(Out o, In i) { cables[(int)i] = (uint16_t)(cables[(int)i] & ~(1u << (int)o)); }
  void clear() {
    for (auto& c : cables) c = 0;
  }
};

} // namespace catacomb
