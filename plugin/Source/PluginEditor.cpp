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

  // The panel is drawn at 1600 × 640 and zoomed to fit, so keep its proportions.
  setResizable(true, true);
  setResizeLimits(1000, 400, 2800, 1120);
  if (auto* c = getConstrainer()) c->setFixedAspectRatio(2.5);
  setSize(1400, 560);
}

void CatacombEditor::resized() { browser.setBounds(getLocalBounds()); }

std::optional<juce::WebBrowserComponent::Resource> CatacombEditor::resource(const juce::String& url) {
  auto path = url.upToFirstOccurrenceOf("?", false, false).trimCharactersAtStart("/");
  if (path.isEmpty()) path = "index.html";
  if (const auto it = files.find(path); it != files.end()) return it->second;
  return std::nullopt;
}

void CatacombEditor::emit(const juce::var& msg) { browser.emitEventIfBrowserIsVisible(kEvent, msg); }

void CatacombEditor::handle(const juce::var& msg) {
  if (msg["type"].toString() != "ready") return;

  // M0: hand the page the sequencer's bits so the LEDs prove the bridge works.
  const auto st = proc.sequencerSnapshot();
  juce::Array<juce::var> bits;
  for (const auto& c : st.mem.cells) bits.add(c.on);
  auto* init = new juce::DynamicObject();
  init->setProperty("type", "init");
  init->setProperty("version", JucePlugin_VersionString);
  init->setProperty("bits", bits);
  init->setProperty("quantMode", st.mem.quantMode);
  emit(juce::var(init));
}

} // namespace catacomb::plugin
