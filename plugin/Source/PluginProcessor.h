// Catacomb AU — the processor. M0: a silent instrument that owns the engine's
// sequencer, so the build, the WebView bridge and auval can be proven end to end.
#pragma once

#include "Sequencer.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace catacomb::plugin {

class CatacombProcessor : public juce::AudioProcessor {
public:
  CatacombProcessor();

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout&) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using AudioProcessor::processBlock;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return "Catacomb"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  double getTailLengthSeconds() const override { return 6.0; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return "Default"; }
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock&) override;
  void setStateInformation(const void*, int) override;

  // For the editor (message thread). M0 only reads it once, at page load.
  Sequencer::State sequencerSnapshot() const;

private:
  Sequencer seq;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CatacombProcessor)
};

} // namespace catacomb::plugin
