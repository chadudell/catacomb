// Catacomb AU — host parameters: every panel knob from the engine's table (Params.h),
// plus the plugin's clock settings, which stand in for the hardware's MIDI clock
// divider and CLOCK 2 jack.
#pragma once

#include "Divisions.h"
#include "Params.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace catacomb::plugin {


namespace ids {
inline const juce::String clockSource = "clockSource";   // 0 host tempo, 1 TEMPO knob
inline const juce::String clockDiv = "clockDiv";         // index into kDivisions
inline const juce::String clock2Div = "clock2Div";       // 0 = same as clock 1, else kDivisions[i-1]
inline const juce::String followTransport = "followTransport";
inline const juce::String resetRecallsBuffer = "resetRecallsBuffer"; // global setting 1,1
inline const juce::String cvOutUnipolar = "cvOutUnipolar";           // global setting 1,2
inline const juce::String midiNotes = "midiNotes";                   // 0 play + transpose, 1 hardware
inline const juce::String outputLimiter = "outputLimiter";
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

  juce::StringArray divisions, divisions2{"Same as Step"};
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
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{ids::midiNotes, 1}, "MIDI Notes",
      juce::StringArray{"Play", "Transpose (hardware)"}, 0));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{ids::outputLimiter, 1}, "Output Limiter", true));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{ids::resetRecallsBuffer, 1}, "RESET Jack Recalls Buffer", false));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{ids::cvOutUnipolar, 1}, "Unipolar CV Outs", false));
  return layout;
}

} // namespace catacomb::plugin
