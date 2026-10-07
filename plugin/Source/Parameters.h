// Catacomb AU — host parameters: every panel knob from the engine's table (Params.h),
// plus the plugin's clock settings, which stand in for the hardware's MIDI clock
// divider and CLOCK 2 jack.
#pragma once

#include "Params.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace catacomb::plugin {

struct Division {
  const char* name;
  double beats; // step length in quarter notes
};

// 16 values, like the hardware's MIDI clock divider (BIT SHIFT 2 + BIT FLIP 2 / LENGTH 2).
constexpr Division kDivisions[16] = {
    {"1/64", 1.0 / 16}, {"1/32T", 1.0 / 12}, {"1/32", 1.0 / 8},  {"1/16T", 1.0 / 6},
    {"1/16", 1.0 / 4},  {"1/8T", 1.0 / 3},   {"1/16.", 3.0 / 8}, {"1/8", 1.0 / 2},
    {"1/4T", 2.0 / 3},  {"1/8.", 3.0 / 4},   {"1/4", 1.0},       {"1/2T", 4.0 / 3},
    {"1/4.", 1.5},      {"1/2", 2.0},        {"1/1", 4.0},       {"2/1", 8.0},
};
constexpr int kDefaultDivision = 4; // 1/16

namespace ids {
inline const juce::String clockSource = "clockSource";   // 0 host tempo, 1 TEMPO knob
inline const juce::String clockDiv = "clockDiv";         // index into kDivisions
inline const juce::String clock2Div = "clock2Div";       // 0 = same as clock 1, else kDivisions[i-1]
inline const juce::String followTransport = "followTransport";
inline const juce::String resetRecallsBuffer = "resetRecallsBuffer"; // global setting 1,1
inline const juce::String cvOutUnipolar = "cvOutUnipolar";           // global setting 1,2
} // namespace ids

inline juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  for (int i = 0; i < kNumParams; i++) {
    const auto& info = paramInfo((Param)i);
    const juce::ParameterID id{info.id, 1};
    if ((Param)i == Param::Order) {
      layout.add(std::make_unique<juce::AudioParameterChoice>(
          id, info.name, juce::StringArray{"Parallel", "VCW > VCF", "VCF > VCW"}, (int)info.def));
    } else {
      layout.add(std::make_unique<juce::AudioParameterFloat>(
          id, info.name, juce::NormalisableRange<float>(info.min, info.max), info.def));
    }
  }

  juce::StringArray divisions, divisions2{"Same as Clock 1"};
  for (const auto& d : kDivisions) {
    divisions.add(d.name);
    divisions2.add(d.name);
  }
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{ids::clockSource, 1}, "Clock Source", juce::StringArray{"Host Tempo", "Tempo Knob"}, 0));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{ids::clockDiv, 1}, "Clock Division", divisions, kDefaultDivision));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{ids::clock2Div, 1}, "SEQ2 Clock Division", divisions2, 0));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{ids::followTransport, 1}, "Follow Transport", true));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{ids::resetRecallsBuffer, 1}, "RESET Jack Recalls Buffer", false));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{ids::cvOutUnipolar, 1}, "Unipolar CV Outs", false));
  return layout;
}

} // namespace catacomb::plugin
