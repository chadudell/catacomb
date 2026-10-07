// Catacomb AU — the processor: host parameters → catacomb::Engine, MIDI in, the clock
// locked to Logic's tempo and transport, and lock-free plumbing to and from the UI.
//
// Threads: the engine (and its sequencer) belong to the audio thread. The UI talks to
// it through a command FIFO; the audio thread publishes a snapshot of the sequencer
// for the UI and for saving. A loaded state is handed over under a lock the audio
// thread only ever try-locks.
#pragma once

#include "Engine.h"
#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace catacomb::plugin {

class CatacombProcessor : public juce::AudioProcessor,
                          private juce::Timer,
                          private juce::AudioProcessorValueTreeState::Listener {
public:
  CatacombProcessor();
  ~CatacombProcessor() override;

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

  juce::AudioProcessorValueTreeState apvts;

  // ---- For the UI (message thread) ---------------------------------------------------
  // Things the UI can ask the sequencer to do.
  struct Command {
    enum Type { Press, Release, SetQuantMode, Reroll, ToggleCell } type;
    int value; // a catacomb::Button for Press/Release, else the argument
  };
  void send(Command c);

  // What the UI shows: the sequencer's memory and where its heads are.
  struct SeqView {
    Sequencer::State state{};
    int playCell[2]{}, writeCell[2]{}, loopLength[2]{};
    bool hostPlaying = false;
    double bpm = 120;
    // Diagnostics, shown in the panel's footer.
    double sampleRate = 0;
    int notesReceived = 0;
    double outputPeak = 0; // since the last view
    long long blocks = 0;
  };
  SeqView seqView() const;

  // Parameters, by id, in their own units (a choice is its index).
  juce::var paramValues(bool onlyChanged); // {id: value}; onlyChanged: since the last call
  juce::var paramMeta() const;             // {id: {name, min, max, def, choices?}}
  void setParamFromUi(const juce::String& id, double value);
  void gestureFromUi(const juce::String& id, bool begin);

  // Panel feedback (LED flashes) since the last call.
  int takePanelEvents(PanelEvent* dest, int max);

  // Patch cables, as text (StateText.h: "mvco>vcwIn,…"). The UI owns the order.
  juce::String patchText() const;
  void setPatchText(const juce::String& text);
  std::atomic<int> patchGeneration{0}; // bumped when a loaded state replaces the cables

  // Bumped when the host loads a state, so an open editor can refresh.
  std::atomic<int> stateGeneration{0};

private:
  void timerCallback() override;
  void parameterChanged(const juce::String& id, float) override;
  std::atomic<bool> anyParamDirty{true};
  juce::StringArray paramIds;
  std::unique_ptr<std::atomic<bool>[]> paramDirty;
  void syncClock(int numSamples);
  void publishView();

  Engine engine;
  std::array<std::atomic<float>*, kNumParams> raw{};
  std::atomic<float>* clockSource = nullptr;
  std::atomic<float>* clockDiv = nullptr;
  std::atomic<float>* clock2Div = nullptr;
  std::atomic<float>* follow = nullptr;

  // UI → audio
  juce::AbstractFifo commandFifo{256};
  std::array<Command, 256> commands{};

  // Loaded state → audio
  juce::SpinLock handoffLock;
  Sequencer::State pendingState{};
  bool statePending = false;
  Patch pendingPatch;
  bool patchPending = false;
  mutable juce::CriticalSection patchLock; // message thread only
  juce::String cables;
  std::atomic<float>* resetRecalls = nullptr;
  std::atomic<float>* unipolar = nullptr;
  std::atomic<float>* midiNotes = nullptr;
  int notesReceived = 0;
  float outputPeak = 0;
  long long blocks = 0;
  std::vector<float> sidechain; // the input bus, mono, copied before we overwrite it

  // Audio → UI / save
  mutable juce::SpinLock viewLock;
  SeqView view;
  int samplesSinceView = 0;
  juce::AbstractFifo eventFifo{128};
  std::array<PanelEvent, 128> events{};
  std::atomic<int> clockDivisionDelta{0}; // from BIT SHIFT 2 + BIT FLIP 2 / LENGTH 2

  // Transport
  bool hostWasPlaying = false;
  double expectedPpq = 0;
  double currentBpm = 120;
  bool currentlyPlaying = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CatacombProcessor)
};

} // namespace catacomb::plugin
