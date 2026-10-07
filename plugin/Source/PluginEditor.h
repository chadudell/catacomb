// Catacomb AU — the editor is the web UI (ui/), embedded in the plugin as a zip and
// shown in a WKWebView. See ui/src/host.js for the message protocol.
#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <map>

namespace catacomb::plugin {

class CatacombEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit CatacombEditor(CatacombProcessor&);
  ~CatacombEditor() override;

  void resized() override;

private:
  void timerCallback() override;
  void handle(const juce::var& msg);
  void emit(const juce::var& msg);
  juce::var viewMessage(const CatacombProcessor::SeqView&) const;
  std::optional<juce::WebBrowserComponent::Resource> resource(const juce::String& url);

  CatacombProcessor& proc;
  std::map<juce::String, juce::WebBrowserComponent::Resource> files;
  juce::WebBrowserComponent browser;
  bool pageReady = false;
  juce::String lastView;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CatacombEditor)
};

} // namespace catacomb::plugin
