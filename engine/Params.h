// Catacomb — every panel knob and switch, in one table. The engine reads them as
// plain floats (p[Param::X]); the plugin builds its host parameters from the same
// table, so the two can't drift apart.
//
// Knobs are 0…1 (noon = 0.5) unless marked bipolar (-1…1, noon = 0). The mapping to
// Hz, seconds or gain lives in the engine, next to the code that uses it.
#pragma once

namespace catacomb {

enum class Param {
  // Oscillators
  VcoFreq, VcoEg1Amt, VcoSeq1Amt, FmAmt,
  MvcoFreq, MvcoEg1Amt, MvcoSeq2Amt,
  // Mixer
  VcoLvl, RingLvl, MvcoLvl, NoiseLvl, NoiseTone,
  // Wavefolder
  Fold, FoldEg1Amt, FoldSeq1Amt, Bias,
  // Filter
  Cutoff, CutoffEg1Amt, CutoffSeq2Amt, Resonance, FilterMode,
  // Blend / amplifiers
  Order, Blend, Volume, UMix1Lvl,
  // Envelopes
  Eg1Decay, Eg2Decay, EgTrigMix,
  // Sequencer
  Tempo, Corrupt1, Corrupt2, CvRange1, CvRange2,
  Count
};

constexpr int kNumParams = (int)Param::Count;

enum Order { OrderParallel = 0, OrderVcwVcf = 1, OrderVcfVcw = 2 };

struct ParamInfo {
  const char* id;    // stable: saved in projects and used for automation
  const char* name;  // shown in the host
  float min, max, def;
  bool smooth;       // continuous knob (smoothed); false = switch
};

// Defaults are the manual's "home base" (p. 12): a clean sine VCO at noon in the mixer,
// no folding, filter open, everything else down.
inline const ParamInfo& paramInfo(Param p) {
  static const ParamInfo table[kNumParams] = {
      {"vcoFreq", "VCO Frequency", 0, 1, 0.5f, true},
      {"vcoEg1Amt", "VCO EG1 Amount", -1, 1, 0, true},
      {"vcoSeq1Amt", "VCO SEQ1 Amount", 0, 1, 0, true},
      {"fmAmt", "MOD VCO → VCO FM", 0, 1, 0, true},
      {"mvcoFreq", "MOD VCO Frequency", 0, 1, 0.5f, true},
      {"mvcoEg1Amt", "MOD VCO EG1 Amount", -1, 1, 0, true},
      {"mvcoSeq2Amt", "MOD VCO SEQ2 Amount", 0, 1, 0, true},
      {"vcoLvl", "VCO Level", 0, 1, 0.5f, true},
      {"ringLvl", "Ring Mod Level", 0, 1, 0, true},
      {"mvcoLvl", "MOD VCO Level", 0, 1, 0, true},
      {"noiseLvl", "Noise Level", 0, 1, 0, true},
      {"noiseTone", "Noise Tone", 0, 1, 0.5f, true},
      {"fold", "VCW Fold", 0, 1, 0, true},
      {"foldEg1Amt", "VCW EG1/CV Amount", -1, 1, 0, true},
      {"foldSeq1Amt", "VCW SEQ1 Amount", 0, 1, 0, true},
      {"bias", "VCW Bias", -1, 1, 0, true},
      {"cutoff", "VCF Cutoff", 0, 1, 1, true},
      {"cutoffEg1Amt", "VCF EG1/CV Amount", -1, 1, 0, true},
      {"cutoffSeq2Amt", "VCF SEQ2 Amount", 0, 1, 0, true},
      {"resonance", "Resonance", 0, 1, 0, true},
      {"filterMode", "Filter Mode", 0, 1, 0, true},
      {"order", "Order", 0, 2, 0, false},
      {"blend", "Blend", 0, 1, 0, true},
      {"volume", "Volume", 0, 1, 0.7f, true},
      {"umix1Lvl", "U Mix 1 Level", 0, 1, 0, true},
      {"eg1Decay", "EG1 Decay", 0, 1, 0.35f, true},
      {"eg2Decay", "EG2 (VCA) Decay", 0, 1, 0.35f, true},
      {"egTrigMix", "EG Trig Mix", 0, 1, 0, true},
      {"tempo", "Tempo", 0, 1, 0.5f, true},
      {"corrupt1", "SEQ1 Corrupt", 0, 1, 0, true},
      {"corrupt2", "SEQ2 Corrupt", 0, 1, 0, true},
      {"cvRange1", "SEQ1 CV Range", 0, 1, 0, true},
      {"cvRange2", "SEQ2 CV Range", 0, 1, 0, true},
  };
  return table[(int)p];
}

struct Params {
  float v[kNumParams];
  Params() {
    for (int i = 0; i < kNumParams; i++) v[i] = paramInfo((Param)i).def;
  }
  float& operator[](Param p) { return v[(int)p]; }
  float operator[](Param p) const { return v[(int)p]; }
};

} // namespace catacomb
