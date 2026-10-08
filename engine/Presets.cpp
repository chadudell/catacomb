#include "Presets.h"

#include "Rng.h"

#include <cmath>
#include <cstring>

namespace catacomb {

namespace {

// Knob positions for frequencies (the curves in Engine.cpp).
float vcoHz(double hz) { return (float)(std::log(hz / 20.0) / std::log(5000.0 / 20.0)); }
float mvcoHz(double hz) { return (float)(std::log(hz / 0.05) / std::log(1300.0 / 0.05)); }
float cutoffHz(double hz) { return (float)(std::log2(hz / 20.0) / 10.0); }

// Scale indices (Quantizer.h, 0-based).
enum { Unquantized = 0, Chromatic, Major, Pentatonic, MelodicMinor, HarmonicMinor, Dim6, WholeTone,
       Hirajoshi, Sus4, Maj7, Maj13, Min7, Min11, HangDrum, Quads };

std::vector<Preset> build() {
  std::vector<Preset> v;

  // The manual's "home base" (p. 12) with SEQ1 playing the VCO: a clean sine melody.
  v.push_back({"Home Base",
               {{"vcoFreq", vcoHz(262)}, {"vcoSeq1Amt", 1}, {"cvRange1", 0.3f}, {"eg2Decay", 0.4f}},
               "10101100", "00000000", Major, "", 4, 8, 8, 11});

  // After "A Simple Start": minor 7th, the manual's bits, a touch of MOD VCO vibrato.
  v.push_back({"First Light",
               {{"vcoFreq", vcoHz(220)}, {"vcoSeq1Amt", 1}, {"cvRange1", 0.3f}, {"fmAmt", 0.1f},
                {"mvcoFreq", mvcoHz(5)}, {"blend", 0.5f}, {"cutoff", cutoffHz(1200)}, {"cutoffSeq2Amt", 0.4f},
                {"cvRange2", 0.5f}, {"resonance", 0.3f}, {"cutoffEg1Amt", 0.3f}, {"eg1Decay", 0.3f},
                {"eg2Decay", 0.42f}, {"egTrigMix", 0.5f}},
               "00111010", "01111010", Min7, "", 4, 8, 8, 21});

  // After "Lost in the Labyrinth": ring-modulated metal, the MOD VCO in the audio range.
  v.push_back({"Ossuary Bells",
               {{"vcoFreq", vcoHz(523)}, {"vcoSeq1Amt", 1}, {"cvRange1", 0.4f}, {"vcoLvl", 0.4f},
                {"ringLvl", 0.6f}, {"mvcoLvl", 0.25f}, {"mvcoFreq", mvcoHz(370)}, {"mvcoSeq2Amt", 1},
                {"cvRange2", 0.3f}, {"fold", 0.25f}, {"eg2Decay", 0.55f}, {"egTrigMix", 0.5f}},
               "10010110", "01001001", Pentatonic, "", 4, 8, 8, 31});

  // After "Dry Brushes": noise through a stepping bandpass, short envelopes.
  v.push_back({"Dust Brushes",
               {{"vcoLvl", 0.3f}, {"noiseLvl", 0.6f}, {"noiseTone", 0.7f}, {"mvcoFreq", mvcoHz(2)},
                {"mvcoLvl", 0.2f}, {"blend", 1}, {"filterMode", 1}, {"cutoff", cutoffHz(2500)},
                {"cutoffSeq2Amt", 0.6f}, {"cvRange2", 0.6f}, {"resonance", 0.5f}, {"cutoffEg1Amt", 0.3f},
                {"eg1Decay", 0.15f}, {"eg2Decay", 0.18f}, {"egTrigMix", 0.5f}, {"corrupt2", 0.2f}},
               "10110101", "01101110", Min7, "", 4, 8, 6, 41});

  // After "Celestial Conversations": hang-drum tuning; the ring mod, via U MIX, as a
  // second voice in the filter path, its speed set by the MOD VCO.
  v.push_back({"Two Voices in the Dark",
               {{"vcoFreq", vcoHz(294)}, {"vcoSeq1Amt", 1}, {"cvRange1", 0.35f}, {"umix1Lvl", 0.55f},
                {"mvcoFreq", mvcoHz(6)}, {"blend", 0.5f}, {"cutoff", cutoffHz(1800)}, {"resonance", 0.4f},
                {"vcoEg1Amt", 0.03f}, {"eg1Decay", 0.45f}, {"eg2Decay", 0.5f}, {"egTrigMix", 0.5f}},
               "10100101", "00100100", HangDrum, "umix12>vcfIn", 4, 8, 8, 51});

  // After "Spiral Enigma"'s tips: the CLOCK patched into VCO 1V/OCT for metallic hats,
  // both sequencers fully corrupt.
  v.push_back({"Bone Hats",
               {{"vcoFreq", vcoHz(1400)}, {"fold", 0.4f}, {"bias", 0.3f}, {"foldSeq1Amt", 0.4f},
                {"cvRange1", 0.4f}, {"noiseLvl", 0.3f}, {"noiseTone", 0.85f}, {"ringLvl", 0.3f},
                {"fmAmt", 0.3f}, {"mvcoFreq", mvcoHz(310)}, {"corrupt1", 1}, {"corrupt2", 1},
                {"eg1Decay", 0.12f}, {"eg2Decay", 0.15f}, {"egTrigMix", 0.5f}, {"volume", 1}},
               "11011011", "10110111", Hirajoshi, "clock>vco1voct", 4, 8, 8, 61});

  // After "Swirling Magenta": unquantized, slow steps, long envelopes, the MOD VCO
  // drifting the folder's bias through the mixer.
  v.push_back({"Slow Spiral",
               {{"vcoFreq", vcoHz(180)}, {"vcoSeq1Amt", 0.6f}, {"cvRange1", 0.2f}, {"fold", 0.3f},
                {"foldEg1Amt", 0.4f}, {"fmAmt", 0.25f}, {"mvcoFreq", mvcoHz(0.6)}, {"mvcoLvl", 0.3f},
                {"blend", 0.4f}, {"cutoff", cutoffHz(1500)}, {"resonance", 0.5f}, {"cutoffSeq2Amt", 0.3f},
                {"cvRange2", 0.6f}, {"eg1Decay", 0.8f}, {"eg2Decay", 0.85f}, {"egTrigMix", 0.5f}},
               "10001000", "00100010", Unquantized, "", 13, 8, 8, 71});

  // After "New Frontiers"' tip: EG2 into BIT FLIP 1 keeps rewriting the drums.
  v.push_back({"Rewriting Drums",
               {{"vcoLvl", 0}, {"mvcoFreq", mvcoHz(45)}, {"mvcoEg1Amt", 0.4f}, {"mvcoLvl", 0.7f},
                {"noiseLvl", 0.5f}, {"blend", 0.45f}, {"cutoff", cutoffHz(4000)}, {"filterMode", 0.6f},
                {"eg1Decay", 0.25f}, {"eg2Decay", 0.2f}, {"corrupt1", 0.3f}, {"egTrigMix", 0.5f},
                {"volume", 0.9f}},
               "10001010", "00100010", Major, "eg2>bitFlip1", 4, 8, 8, 81});

  // After "Syndrums": pentatonic toms, a pitch sweep from EG1, a little FM.
  v.push_back({"Pentatonic Toms",
               {{"vcoFreq", vcoHz(140)}, {"vcoSeq1Amt", 1}, {"cvRange1", 0.35f}, {"vcoEg1Amt", 0.25f},
                {"eg1Decay", 0.2f}, {"fmAmt", 0.2f}, {"mvcoFreq", mvcoHz(160)}, {"mvcoLvl", 0.2f},
                {"fold", 0.2f}, {"blend", 0.3f}, {"cutoff", cutoffHz(2500)}, {"resonance", 0.35f},
                {"eg2Decay", 0.3f}, {"egTrigMix", 0.5f}},
               "10110110", "01011010", Pentatonic, "", 4, 8, 8, 91});

  // After "Telemechanical Birds": EG1 chirps a high, FM'd VCO; triplet steps.
  v.push_back({"Clockwork Birds",
               {{"vcoFreq", vcoHz(1800)}, {"vcoEg1Amt", 0.55f}, {"eg1Decay", 0.15f}, {"fmAmt", 0.35f},
                {"mvcoFreq", mvcoHz(170)}, {"vcoSeq1Amt", 0.6f}, {"cvRange1", 0.4f}, {"corrupt1", 0.4f},
                {"eg2Decay", 0.25f}, {"egTrigMix", 0.5f}},
               "10100100", "01000101", Chromatic, "", 3, 8, 8, 101});

  // After "Where You Lead Kicks Follow": EG1 decays the folder path (a kick on the MOD
  // VCO), EG2 the filter path (the lead).
  v.push_back({"Kick & Follow",
               {{"mvcoFreq", mvcoHz(40)}, {"mvcoEg1Amt", 0.45f}, {"mvcoLvl", 0.75f}, {"vcoLvl", 0.45f},
                {"vcoFreq", vcoHz(330)}, {"vcoSeq1Amt", 1}, {"cvRange1", 0.35f}, {"blend", 0.5f},
                {"cutoff", cutoffHz(1600)}, {"cutoffEg1Amt", 0.3f}, {"resonance", 0.4f},
                {"eg1Decay", 0.25f}, {"eg2Decay", 0.5f}, {"egTrigMix", 0.5f}},
               "10011000", "00100101", Min7, "eg1>vcwVcaCv", 4, 8, 8, 111});

  // After "Twinkle Toes": two melodic voices, VCO on SEQ1 and MOD VCO on SEQ2.
  v.push_back({"Sparkle Steps",
               {{"vcoFreq", vcoHz(392)}, {"vcoSeq1Amt", 1}, {"cvRange1", 0.4f}, {"vcoLvl", 0.5f},
                {"mvcoFreq", mvcoHz(523)}, {"mvcoSeq2Amt", 1}, {"cvRange2", 0.4f}, {"mvcoLvl", 0.5f},
                {"fold", 0.2f}, {"blend", 0.5f}, {"cutoff", cutoffHz(5000)}, {"filterMode", 0.3f},
                {"eg2Decay", 0.3f}, {"egTrigMix", 0.5f}},
               "10101010", "01010101", Major, "", 4, 8, 7, 121});

  // After "The Ooze": the ring mod, via U MIX, wobbling the fold amount.
  v.push_back({"Tar Pit",
               {{"vcoFreq", vcoHz(110)}, {"vcoSeq1Amt", 1}, {"cvRange1", 0.3f}, {"umix1Lvl", 0.5f},
                {"foldEg1Amt", 0.6f}, {"fold", 0.3f}, {"mvcoFreq", mvcoHz(75)}, {"noiseLvl", 0.15f},
                {"blend", 0.3f}, {"cutoff", cutoffHz(1200)}, {"resonance", 0.5f}, {"eg2Decay", 0.6f},
                {"egTrigMix", 0.5f}},
               "10010010", "01001000", Min11, "umix12>fold", 4, 8, 8, 131});

  return v;
}

} // namespace

const std::vector<Preset>& factoryPresets() {
  static const std::vector<Preset> presets = build();
  return presets;
}

Params presetParams(const Preset& p) {
  Params out;
  for (const auto& [id, value] : p.params)
    for (int i = 0; i < kNumParams; i++)
      if (std::strcmp(paramInfo((Param)i).id, id) == 0) out.v[i] = value;
  return out;
}

Sequencer::State presetSequencer(const Preset& p, const Sequencer::State& base) {
  Sequencer::State s = base;
  Rng rng(p.seed);
  for (int c = 0; c < kCells; c++) {
    const char* bits = c < kBits ? p.bits1 : p.bits2;
    s.mem.cells[c].on = bits[c % kBits] == '1';
    s.mem.cells[c].volts = (float)rng.uniform(-5.0, 5.0);
    s.mem.origin[c] = (uint8_t)c;
  }
  s.mem.length[0] = p.length1;
  s.mem.length[1] = p.length2;
  s.mem.writeOffset[0] = s.mem.writeOffset[1] = 0;
  s.mem.quantMode = p.scale;
  s.mem.chained = false;
  s.saved = s.mem;
  s.bufferValid = true; // BUFFER recalls the preset's pattern
  s.pos[0] = s.pos[1] = 0;
  s.held[0] = s.held[1] = 0;
  s.rootSemis = 0;
  for (int i = 0; i < 4; i++) s.rng[i] = rng.s[i];
  return s;
}

} // namespace catacomb
