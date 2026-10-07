#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "StateText.h"

#include <cmath>

namespace catacomb::plugin {

namespace {

constexpr double kGridTolerance = 1e-6; // beats: "on a step boundary"

double fractional(double x) { return x - std::floor(x); }

} // namespace

CatacombProcessor::CatacombProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Catacomb", makeLayout()) {
  for (int i = 0; i < kNumParams; i++) raw[(size_t)i] = apvts.getRawParameterValue(paramInfo((Param)i).id);
  clockSource = apvts.getRawParameterValue(ids::clockSource);
  clockDiv = apvts.getRawParameterValue(ids::clockDiv);
  clock2Div = apvts.getRawParameterValue(ids::clock2Div);
  follow = apvts.getRawParameterValue(ids::followTransport);
  resetRecalls = apvts.getRawParameterValue(ids::resetRecallsBuffer);
  unipolar = apvts.getRawParameterValue(ids::cvOutUnipolar);
  sidechain.assign(4096, 0.0f);

  for (auto* param : getParameters())
    if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) paramIds.add(withId->paramID);
  paramDirty = std::make_unique<std::atomic<bool>[]>((size_t)paramIds.size());
  for (int i = 0; i < paramIds.size(); i++) {
    paramDirty[(size_t)i] = true;
    apvts.addParameterListener(paramIds[i], this);
  }

  engine.seq.factoryPattern(); // a new unit comes with sequences in it
  view.state = engine.seq.state();
  startTimerHz(10);
}

CatacombProcessor::~CatacombProcessor() {
  stopTimer();
  for (const auto& id : paramIds) apvts.removeParameterListener(id, this);
}

bool CatacombProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  const auto out = layouts.getMainOutputChannelSet();
  const auto in = layouts.getMainInputChannelSet();
  const auto ok = [](const juce::AudioChannelSet& s) {
    return s == juce::AudioChannelSet::stereo() || s == juce::AudioChannelSet::mono();
  };
  return ok(out) && (in.isDisabled() || ok(in));
}

void CatacombProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  engine.prepare(sampleRate);
  sidechain.assign((size_t)std::max(samplesPerBlock, 4096), 0.0f);
  hostWasPlaying = false;
}

// ---- Audio thread --------------------------------------------------------------------------

void CatacombProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  const int n = buffer.getNumSamples();

  {
    const juce::SpinLock::ScopedTryLockType l(handoffLock);
    if (l.isLocked() && statePending) {
      engine.seq.setState(pendingState);
      statePending = false;
    }
    if (l.isLocked() && patchPending) {
      engine.patch = pendingPatch;
      patchPending = false;
    }
  }

  {
    const auto r = commandFifo.read(commandFifo.getNumReady());
    auto run = [&](int start, int size) {
      for (int i = 0; i < size; i++) {
        const Command& c = commands[(size_t)(start + i)];
        switch (c.type) {
          case Command::Press: engine.press((Button)c.value); break;
          case Command::Release: engine.release((Button)c.value); break;
          case Command::SetQuantMode: engine.seq.setQuantMode(c.value); break;
          case Command::Reroll: engine.seq.reroll(); break;
          case Command::ToggleCell: engine.seq.toggleCell(c.value); break;
        }
      }
    };
    run(r.startIndex1, r.blockSize1);
    run(r.startIndex2, r.blockSize2);
  }

  for (int i = 0; i < kNumParams; i++) engine.params.v[i] = raw[(size_t)i]->load(std::memory_order_relaxed);
  engine.resetRecallsBuffer = resetRecalls->load() > 0.5f;
  engine.unipolarCvOut = unipolar->load() > 0.5f;

  // The sidechain shares channels with the output, so take a (mono) copy first.
  const float* side = nullptr;
  if (getBusCount(true) > 0 && getBus(true, 0)->isEnabled() && n <= (int)sidechain.size()) {
    const auto in = getBusBuffer(buffer, true, 0);
    if (in.getNumChannels() > 0) {
      const float* l = in.getReadPointer(0);
      const float* r = in.getNumChannels() > 1 ? in.getReadPointer(1) : l;
      for (int i = 0; i < n; i++) sidechain[(size_t)i] = 0.5f * (l[i] + r[i]);
      side = sidechain.data();
    }
  }

  syncClock(n);

  // Render in segments between MIDI events, so notes land on their sample.
  auto* left = buffer.getWritePointer(0);
  int done = 0;
  for (const auto meta : midi) {
    const int at = juce::jlimit(done, n, meta.samplePosition);
    if (at > done) engine.process(left + done, at - done, side ? side + done : nullptr);
    done = at;
    const auto m = meta.getMessage();
    if (m.isNoteOn()) {
      engine.noteOn(m.getNoteNumber(), m.getFloatVelocity());
    } else if (m.isMidiStart() || (m.isSongPositionPointer() && m.getSongPositionPointerMidiBeat() == 0)) {
      engine.seq.reset();
      engine.resetVoice();
      engine.setClockPhase(0, 0);
      engine.setClockPhase(1, 0);
      engine.armClocks(true);
      if (m.isMidiStart()) engine.seq.setRunning(true);
    } else if (m.isMidiStop()) {
      engine.seq.setRunning(false);
    } else if (m.isMidiContinue()) {
      engine.seq.setRunning(true);
    }
  }
  if (done < n) engine.process(left + done, n - done, side ? side + done : nullptr);
  midi.clear();

  for (int c = 1; c < buffer.getNumChannels(); c++) buffer.copyFrom(c, 0, buffer, 0, 0, n);

  // Panel events: clock-division requests become a parameter change (on the message
  // thread); the rest go to the UI.
  PanelEvent ev[Panel::kMaxEvents];
  const int ne = engine.takeEvents(ev, Panel::kMaxEvents);
  for (int i = 0; i < ne; i++) {
    if (ev[i].type == PanelEvent::ClockDivision) {
      clockDivisionDelta += ev[i].value;
    } else {
      const auto w = eventFifo.write(1);
      if (w.blockSize1 > 0) events[(size_t)w.startIndex1] = ev[i];
    }
  }

  samplesSinceView += n;
  if (samplesSinceView >= getSampleRate() / 60) publishView();
}

// Host tempo: each step is a fixed division of a beat, locked to Logic's grid while it
// plays and free-running at the project tempo while it's stopped. TEMPO knob: the
// hardware's internal clock.
void CatacombProcessor::syncClock(int numSamples) {
  double bpm = 120, ppq = 0;
  bool playing = false, hasPpq = false;
  if (auto* ph = getPlayHead()) {
    if (const auto pos = ph->getPosition()) {
      if (const auto b = pos->getBpm()) bpm = *b;
      playing = pos->getIsPlaying();
      if (const auto p = pos->getPpqPosition()) {
        ppq = *p;
        hasPpq = true;
      }
    }
  }
  currentBpm = bpm;
  currentlyPlaying = playing;

  const bool useHost = clockSource->load() < 0.5f;
  const bool follows = follow->load() > 0.5f;
  const int div1 = juce::jlimit(0, 15, (int)clockDiv->load());
  const int div2Choice = juce::jlimit(0, 16, (int)clock2Div->load());
  const double beats1 = kDivisions[div1].beats;
  const double beats2 = div2Choice == 0 ? beats1 : kDivisions[div2Choice - 1].beats;
  const bool separate2 = div2Choice != 0 && div2Choice - 1 != div1;

  if (useHost) {
    const double stepsPerSecond = bpm / 60.0;
    engine.setClockRates(stepsPerSecond / beats1, separate2 ? stepsPerSecond / beats2 : 0);
  } else {
    const double hz = Engine::tempoHz(raw[(size_t)Param::Tempo]->load());
    engine.setClockRates(0, separate2 ? hz * beats1 / beats2 : 0);
  }

  const bool started = playing && !hostWasPlaying;
  const bool jumped = playing && hostWasPlaying && hasPpq && std::abs(ppq - expectedPpq) > 1e-3;

  if (useHost && playing && hasPpq) {
    const double beatsLine[2] = {beats1, separate2 ? beats2 : beats1};
    if (started || jumped) {
      // Put each head where the song position says: the next step boundary plays the
      // bit for that step (counting from bar 1, beat 1 = bit 1).
      bool onBoundary = false;
      for (int line = 0; line < 2; line++) {
        const double steps = ppq / beatsLine[line];
        const double next = std::ceil(steps - kGridTolerance);
        const bool on = std::abs(steps - next) < kGridTolerance;
        if (line == 0) onBoundary = on;
        engine.seq.setPlayStep(line, (int)((long long)next - 1));
        engine.setClockPhase(line, on ? 0.0 : fractional(steps));
      }
      engine.armClocks(onBoundary);
    } else {
      // Keep the free-running phase locked to the grid (tiny corrections only).
      for (int line = 0; line < 2; line++) {
        const double want = fractional(ppq / beatsLine[line]);
        double diff = engine.clockPhaseOf(line) - want;
        diff -= std::round(diff);
        if (std::abs(diff) > 0.002) engine.setClockPhase(line, want);
      }
    }
  } else if (!useHost && started && follows) {
    // TEMPO knob, following the transport: start from bit 1 like a MIDI Start.
    engine.seq.reset();
    engine.setClockPhase(0, 0);
    engine.setClockPhase(1, 0);
    engine.armClocks(true);
  }

  if (started) engine.resetVoice();
  if (follows) {
    if (started) engine.seq.setRunning(true);
    else if (!playing && hostWasPlaying) engine.seq.setRunning(false);
  }

  hostWasPlaying = playing;
  expectedPpq = ppq + numSamples / getSampleRate() * bpm / 60.0;
}

void CatacombProcessor::publishView() {
  const juce::SpinLock::ScopedTryLockType l(viewLock);
  if (!l.isLocked()) return;
  samplesSinceView = 0;
  view.state = engine.seq.state();
  for (int h = 0; h < 2; h++) {
    view.playCell[h] = engine.seq.playCell(h);
    view.writeCell[h] = engine.seq.writeCell(h);
    view.loopLength[h] = engine.seq.loopLength(h);
  }
  view.hostPlaying = currentlyPlaying;
  view.bpm = currentBpm;
}

// ---- Message thread ------------------------------------------------------------------------

void CatacombProcessor::send(Command c) {
  const auto w = commandFifo.write(1);
  if (w.blockSize1 > 0) commands[(size_t)w.startIndex1] = c;
}

CatacombProcessor::SeqView CatacombProcessor::seqView() const {
  const juce::SpinLock::ScopedLockType l(viewLock);
  return view;
}

int CatacombProcessor::takePanelEvents(PanelEvent* dest, int max) {
  const auto r = eventFifo.read(std::min(eventFifo.getNumReady(), max));
  int k = 0;
  for (int i = 0; i < r.blockSize1; i++) dest[k++] = events[(size_t)(r.startIndex1 + i)];
  for (int i = 0; i < r.blockSize2; i++) dest[k++] = events[(size_t)(r.startIndex2 + i)];
  return k;
}

void CatacombProcessor::parameterChanged(const juce::String& id, float) {
  const int i = paramIds.indexOf(id);
  if (i < 0) return;
  paramDirty[(size_t)i] = true;
  anyParamDirty = true;
}

static double valueOf(juce::RangedAudioParameter* p) {
  return p->convertFrom0to1(p->getValue()); // a choice comes back as its index
}

juce::var CatacombProcessor::paramValues(bool onlyChanged) {
  if (onlyChanged && !anyParamDirty.exchange(false)) return {};
  if (!onlyChanged) anyParamDirty = false;
  auto* obj = new juce::DynamicObject();
  for (int i = 0; i < paramIds.size(); i++) {
    const bool dirty = paramDirty[(size_t)i].exchange(false);
    if (onlyChanged && !dirty) continue;
    obj->setProperty(paramIds[i], valueOf(apvts.getParameter(paramIds[i])));
  }
  return juce::var(obj);
}

juce::var CatacombProcessor::paramMeta() const {
  auto* obj = new juce::DynamicObject();
  for (const auto& id : paramIds) {
    auto* p = apvts.getParameter(id);
    const auto range = p->getNormalisableRange();
    auto* m = new juce::DynamicObject();
    m->setProperty("name", p->getName(64));
    m->setProperty("min", range.start);
    m->setProperty("max", range.end);
    m->setProperty("def", p->convertFrom0to1(p->getDefaultValue()));
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p)) {
      juce::Array<juce::var> names;
      for (const auto& c : choice->choices) names.add(c);
      m->setProperty("choices", names);
    }
    obj->setProperty(id, juce::var(m));
  }
  return juce::var(obj);
}

void CatacombProcessor::setParamFromUi(const juce::String& id, double value) {
  if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1((float)value));
}

void CatacombProcessor::gestureFromUi(const juce::String& id, bool begin) {
  if (auto* p = apvts.getParameter(id)) begin ? p->beginChangeGesture() : p->endChangeGesture();
}

void CatacombProcessor::timerCallback() {
  // The panel's clock-division combo moves the Clock Division parameter (so the host
  // sees and saves it).
  if (const int delta = clockDivisionDelta.exchange(0); delta != 0) {
    auto* p = apvts.getParameter(ids::clockDiv);
    const int next = juce::jlimit(0, 15, (int)clockDiv->load() + delta);
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1((float)next));
    p->endChangeGesture();
  }
}

juce::String CatacombProcessor::patchText() const {
  const juce::ScopedLock l(patchLock);
  return cables;
}

void CatacombProcessor::setPatchText(const juce::String& text) {
  const auto list = decodeCables(text.toStdString());
  {
    const juce::ScopedLock l(patchLock);
    cables = juce::String(encodeCables(list)); // normalised: unknown jacks dropped
  }
  const juce::SpinLock::ScopedLockType h(handoffLock);
  pendingPatch = patchFrom(list);
  patchPending = true;
}

juce::AudioProcessorEditor* CatacombProcessor::createEditor() { return new CatacombEditor(*this); }

// ---- State ---------------------------------------------------------------------------------

void CatacombProcessor::getStateInformation(juce::MemoryBlock& dest) {
  auto state = apvts.copyState();
  Sequencer::State seqState;
  {
    const juce::SpinLock::ScopedLockType h(handoffLock);
    seqState = statePending ? pendingState : seqView().state;
  }
  juce::ValueTree seq("Sequencer");
  seq.setProperty("text", juce::String(encodeSequencer(seqState)), nullptr);
  state.appendChild(seq, nullptr);
  juce::ValueTree patch("Patch");
  patch.setProperty("cables", patchText(), nullptr);
  state.appendChild(patch, nullptr);
  state.setProperty("version", JucePlugin_VersionString, nullptr);
  if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void CatacombProcessor::setStateInformation(const void* data, int size) {
  auto xml = getXmlFromBinary(data, size);
  if (!xml) return;
  auto state = juce::ValueTree::fromXml(*xml);
  if (!state.hasType(apvts.state.getType())) return;
  auto seq = state.getChildWithName("Sequencer");
  state.removeChild(seq, nullptr);
  auto patch = state.getChildWithName("Patch");
  state.removeChild(patch, nullptr);
  apvts.replaceState(state);
  setPatchText(patch.isValid() ? patch["cables"].toString() : juce::String());
  patchGeneration++;

  Sequencer::State decoded = seqView().state;
  if (seq.isValid() && decodeSequencer(seq["text"].toString().toStdString(), decoded)) {
    {
      const juce::SpinLock::ScopedLockType h(handoffLock);
      pendingState = decoded;
      statePending = true;
    }
    const juce::SpinLock::ScopedLockType v(viewLock);
    view.state = decoded;
  }
  stateGeneration++;
}

} // namespace catacomb::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new catacomb::plugin::CatacombProcessor(); }
