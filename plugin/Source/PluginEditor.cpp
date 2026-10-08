#include "PluginEditor.h"

#include "BinaryData.h"

namespace catacomb::plugin {

namespace {

const juce::Identifier kEvent{"cat"};

const char* mimeFor(const juce::String& path) {
  const auto ext = path.fromLastOccurrenceOf(".", false, false).toLowerCase();
  if (ext == "html") return "text/html";
  if (ext == "js" || ext == "mjs") return "text/javascript";
  if (ext == "css") return "text/css";
  if (ext == "png") return "image/png";
  if (ext == "svg") return "image/svg+xml";
  if (ext == "woff2") return "font/woff2";
  if (ext == "json") return "application/json";
  return "application/octet-stream";
}

// The UI names buttons as in the Button enum.
constexpr const char* kButtonNames[(int)Button::Count] = {
    "Buffer", "Reset", "Advance", "Chain", "RunStop", "Trigger",
    "Length1", "Shift1", "Flip1", "Length2", "Shift2", "Flip2",
};

int buttonIndex(const juce::String& name) {
  for (int i = 0; i < (int)Button::Count; i++)
    if (name == kButtonNames[i]) return i;
  return -1;
}

const char* eventName(PanelEvent::Type t) {
  switch (t) {
    case PanelEvent::ManualTrigger: return "trigger";
    case PanelEvent::ClockDivision: return "clockDivision";
    case PanelEvent::ShowLength: return "showLength";
    case PanelEvent::ShowQuantMode: return "showQuantMode";
    case PanelEvent::BufferSaved: return "bufferSaved";
    case PanelEvent::BufferRecalled: return "bufferRecalled";
    case PanelEvent::Cleared: return "cleared";
  }
  return "";
}

} // namespace

CatacombEditor::CatacombEditor(CatacombProcessor& p)
    : AudioProcessorEditor(p),
      proc(p),
      browser(juce::WebBrowserComponent::Options{}
                  .withNativeIntegrationEnabled()
                  .withKeepPageLoadedWhenBrowserIsHidden()
                  .withResourceProvider([this](const juce::String& url) { return resource(url); })
                  .withEventListener(kEvent, [this](const juce::var& msg) { handle(msg); })) {
  // The UI ships inside the plugin as one zip of ui/.
  juce::MemoryInputStream zipStream(BinaryData::ui_zip, (size_t)BinaryData::ui_zipSize, false);
  juce::ZipFile zip(zipStream);
  for (int i = 0; i < zip.getNumEntries(); i++) {
    const auto* entry = zip.getEntry(i);
    if (entry->filename.endsWithChar('/')) continue;
    std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(i));
    if (!in) continue;
    juce::MemoryBlock data;
    in->readIntoMemoryBlock(data);
    const auto* bytes = static_cast<const std::byte*>(data.getData());
    files[entry->filename] = {std::vector<std::byte>(bytes, bytes + data.getSize()), mimeFor(entry->filename)};
  }

  addAndMakeVisible(browser);
  browser.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

  // The panel is drawn at 1800 × 720 and zoomed to fit, so keep its proportions.
  setResizable(true, true);
  setResizeLimits(1100, 440, 3000, 1200);
  if (auto* c = getConstrainer()) c->setFixedAspectRatio(2.5);
  setSize(1500, 600);
  startTimerHz(30);
}

CatacombEditor::~CatacombEditor() { stopTimer(); }

void CatacombEditor::resized() { browser.setBounds(getLocalBounds()); }

std::optional<juce::WebBrowserComponent::Resource> CatacombEditor::resource(const juce::String& url) {
  auto path = url.upToFirstOccurrenceOf("?", false, false).trimCharactersAtStart("/");
  if (path.isEmpty()) path = "index.html";
  if (const auto it = files.find(path); it != files.end()) return it->second;
  return std::nullopt;
}

void CatacombEditor::emit(const juce::var& msg) { browser.emitEventIfBrowserIsVisible(kEvent, msg); }

void CatacombEditor::handle(const juce::var& msg) {
  const auto type = msg["type"].toString();
  if (type == "ready") {
    pageReady = true;
    lastView.clear();
    auto* init = new juce::DynamicObject();
    init->setProperty("type", "init");
    init->setProperty("version", JucePlugin_VersionString);
    init->setProperty("meta", proc.paramMeta());
    init->setProperty("params", proc.paramValues(false));
    init->setProperty("cables", proc.patchText());
    juce::Array<juce::var> presets;
    for (int i = 0; i < proc.getNumPrograms(); i++) presets.add(proc.getProgramName(i));
    init->setProperty("presets", presets);
    init->setProperty("preset", proc.getCurrentProgram());
    seenPatchGeneration = proc.patchGeneration.load();
    emit(juce::var(init));
    timerCallback();
  } else if (type == "press" || type == "release") {
    if (const int b = buttonIndex(msg["button"].toString()); b >= 0)
      proc.send({type == "press" ? CatacombProcessor::Command::Press : CatacombProcessor::Command::Release, b});
  } else if (type == "param") {
    proc.setParamFromUi(msg["id"].toString(), (double)msg["value"]);
  } else if (type == "gesture") {
    proc.gestureFromUi(msg["id"].toString(), (bool)msg["begin"]);
  } else if (type == "loadPreset") {
    proc.setCurrentProgram((int)msg["index"]);
    proc.updateHostDisplay(juce::AudioProcessor::ChangeDetails().withProgramChanged(true));
  } else if (type == "patch") {
    proc.setPatchText(msg["cables"].toString());
  } else if (type == "toggleCell") {
    proc.send({CatacombProcessor::Command::ToggleCell, (int)msg["cell"]});
  } else if (type == "setQuantMode") {
    proc.send({CatacombProcessor::Command::SetQuantMode, (int)msg["value"]});
  } else if (type == "reroll") {
    proc.send({CatacombProcessor::Command::Reroll, 0});
  }
}

juce::var CatacombEditor::viewMessage(const CatacombProcessor::SeqView& v) const {
  juce::Array<juce::var> bits, volts;
  for (const auto& c : v.state.mem.cells) {
    bits.add(c.on);
    volts.add(std::round(c.volts * 1000.0) / 1000.0);
  }
  auto pair = [](const int* a) { return juce::Array<juce::var>{a[0], a[1]}; };
  auto* m = new juce::DynamicObject();
  m->setProperty("type", "seq");
  m->setProperty("bits", bits);
  m->setProperty("volts", volts);
  m->setProperty("play", pair(v.playCell));
  m->setProperty("write", pair(v.writeCell));
  m->setProperty("length", pair(v.state.mem.length));
  m->setProperty("loopLength", pair(v.loopLength));
  m->setProperty("quantMode", v.state.mem.quantMode);
  m->setProperty("chained", v.state.mem.chained);
  m->setProperty("running", v.state.running);
  m->setProperty("hasBuffer", v.state.bufferValid);
  m->setProperty("hostPlaying", v.hostPlaying);
  m->setProperty("bpm", v.bpm);
  m->setProperty("sampleRate", v.sampleRate);
  m->setProperty("notes", v.notesReceived);
  m->setProperty("peakDb", std::round(juce::Decibels::gainToDecibels(v.outputPeak, -100.0)));
  m->setProperty("audio", v.blocks > 0);
  return juce::var(m);
}

void CatacombEditor::timerCallback() {
  if (!pageReady) return;
  juce::Array<juce::var> batch;

  // Parameters that moved (automation, the host's own controls, a loaded project).
  if (const auto changed = proc.paramValues(true); changed.isObject()) {
    auto* m = new juce::DynamicObject();
    m->setProperty("type", "params");
    m->setProperty("values", changed);
    batch.add(juce::var(m));
  }

  // A loaded project replaced the cables.
  if (const int gen = proc.patchGeneration.load(); gen != seenPatchGeneration) {
    seenPatchGeneration = gen;
    auto* m = new juce::DynamicObject();
    m->setProperty("type", "patch");
    m->setProperty("cables", proc.patchText());
    batch.add(juce::var(m));
  }

  // The sequencer view, only when something changed.
  const auto view = viewMessage(proc.seqView());
  const auto json = juce::JSON::toString(view, true);
  if (json != lastView) {
    lastView = json;
    batch.add(view);
  }

  PanelEvent ev[32];
  const int n = proc.takePanelEvents(ev, 32);
  for (int i = 0; i < n; i++) {
    auto* m = new juce::DynamicObject();
    m->setProperty("type", "panel");
    m->setProperty("event", eventName(ev[i].type));
    m->setProperty("value", ev[i].value);
    batch.add(juce::var(m));
  }

  // One script call per tick (each call wakes WebKit's helper processes).
  if (batch.isEmpty()) return;
  auto* m = new juce::DynamicObject();
  m->setProperty("type", "batch");
  m->setProperty("msgs", batch);
  emit(juce::var(m));
}

} // namespace catacomb::plugin
