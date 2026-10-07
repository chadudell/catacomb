#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace catacomb::plugin {

CatacombProcessor::CatacombProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
  seq.factoryPattern(); // a new unit comes with sequences in it
}

bool CatacombProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  const auto out = layouts.getMainOutputChannelSet();
  return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void CatacombProcessor::prepareToPlay(double, int) {}

void CatacombProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  midi.clear();
  buffer.clear();
}

juce::AudioProcessorEditor* CatacombProcessor::createEditor() { return new CatacombEditor(*this); }

Sequencer::State CatacombProcessor::sequencerSnapshot() const { return seq.state(); }

// State arrives in M3 (parameters + sequencer memory + RNG); for now, nothing to save.
void CatacombProcessor::getStateInformation(juce::MemoryBlock&) {}
void CatacombProcessor::setStateInformation(const void*, int) {}

} // namespace catacomb::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new catacomb::plugin::CatacombProcessor(); }
