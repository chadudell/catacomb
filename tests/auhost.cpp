// A minimal Audio Unit host that loads the *installed* Catacomb.component the way Logic
// does, and checks the plugin layer: MIDI notes play while stopped, steps lock to the
// host's beat grid while playing, and a saved state (ClassInfo) restores to an
// instance that then plays identically. Run: tests/auhost.sh
#include <AudioToolbox/AudioToolbox.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr double kFs = 48000;
constexpr UInt32 kBlock = 512;
int failures = 0;

void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
  if (!ok) failures++;
}

struct Transport {
  bool playing = false;
  double bpm = 120;
  double sample = 0; // timeline position
  double beat() const { return sample / kFs * bpm / 60.0; }
};

OSStatus beatAndTempo(void* user, Float64* beat, Float64* tempo) {
  auto* t = static_cast<Transport*>(user);
  if (beat) *beat = t->beat();
  if (tempo) *tempo = t->bpm;
  return noErr;
}

OSStatus transportState2(void* user, Boolean* playing, Boolean* recording, Boolean* changed, Float64* sample,
                         Boolean* cycling, Float64* cycleStart, Float64* cycleEnd) {
  auto* t = static_cast<Transport*>(user);
  if (playing) *playing = t->playing;
  if (recording) *recording = false;
  if (changed) *changed = false;
  if (sample) *sample = t->sample;
  if (cycling) *cycling = false;
  if (cycleStart) *cycleStart = 0;
  if (cycleEnd) *cycleEnd = 0;
  return noErr;
}

OSStatus transportState1(void* user, Boolean* playing, Boolean* changed, Float64* sample, Boolean* cycling,
                         Float64* cycleStart, Float64* cycleEnd) {
  Boolean recording;
  return transportState2(user, playing, &recording, changed, sample, cycling, cycleStart, cycleEnd);
}

OSStatus musicalTime(void* user, UInt32* delta, Float32* num, UInt32* den, Float64* downbeat) {
  auto* t = static_cast<Transport*>(user);
  if (delta) *delta = 0;
  if (num) *num = 4;
  if (den) *den = 4;
  if (downbeat) *downbeat = std::floor(t->beat() / 4) * 4;
  return noErr;
}

struct Instance {
  AudioUnit au = nullptr;
  Transport transport;
  Float64 sampleTime = 0;

  bool open() {
    AudioComponentDescription d{kAudioUnitType_MusicDevice, 'Cat1', 'Ctcb', 0, 0};
    AudioComponent c = AudioComponentFindNext(nullptr, &d);
    if (!c || AudioComponentInstanceNew(c, &au) != noErr) return false;
    AudioStreamBasicDescription f{};
    f.mSampleRate = kFs;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = f.mBytesPerFrame = 4;
    f.mFramesPerPacket = 1;
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel = 32;
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
    UInt32 maxFrames = kBlock;
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof maxFrames);
    HostCallbackInfo cb{};
    cb.hostUserData = &transport;
    cb.beatAndTempoProc = beatAndTempo;
    cb.musicalTimeLocationProc = musicalTime;
    cb.transportStateProc = transportState1;
    cb.transportStateProc2 = transportState2;
    AudioUnitSetProperty(au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &cb, sizeof cb);
    return AudioUnitInitialize(au) == noErr;
  }
  ~Instance() {
    if (au) {
      AudioUnitUninitialize(au);
      AudioComponentInstanceDispose(au);
    }
  }

  // Renders `seconds` of mono (left channel) audio.
  std::vector<float> render(double seconds) {
    std::vector<float> out;
    std::vector<float> l(kBlock), r(kBlock);
    const size_t total = (size_t)(seconds * kFs);
    while (out.size() < total) {
      struct { AudioBufferList list; AudioBuffer second; } buf{};
      buf.list.mNumberBuffers = 2;
      buf.list.mBuffers[0] = {1, kBlock * 4, l.data()};
      buf.second = {1, kBlock * 4, r.data()};
      AudioTimeStamp ts{};
      ts.mSampleTime = sampleTime;
      ts.mFlags = kAudioTimeStampSampleTimeValid;
      AudioUnitRenderActionFlags flags = 0;
      if (AudioUnitRender(au, &flags, &ts, 0, kBlock, &buf.list) != noErr) break;
      out.insert(out.end(), l.begin(), l.end());
      sampleTime += kBlock;
      if (transport.playing) transport.sample += kBlock;
    }
    out.resize(total);
    return out;
  }

  // Sets a parameter by its display name.
  bool setParam(const char* name, float value) {
    UInt32 size = 0;
    AudioUnitGetPropertyInfo(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, nullptr);
    std::vector<AudioUnitParameterID> ids(size / sizeof(AudioUnitParameterID));
    AudioUnitGetProperty(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &size);
    for (auto id : ids) {
      AudioUnitParameterInfo info{};
      UInt32 is = sizeof info;
      AudioUnitGetProperty(au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &is);
      if (info.cfNameString && CFStringCompare(info.cfNameString, CFStringCreateWithCString(nullptr, name, kCFStringEncodingUTF8), 0) == kCFCompareEqualTo)
        return AudioUnitSetParameter(au, id, kAudioUnitScope_Global, 0, value, 0) == noErr;
    }
    return false;
  }

  CFPropertyListRef saveState() {
    CFPropertyListRef state = nullptr;
    UInt32 size = sizeof state;
    AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &size);
    return state;
  }
  bool loadState(CFPropertyListRef state) {
    return AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof state) == noErr;
  }
};

// A copy of a saved state with its cables replaced. JUCE keeps the state as binary XML
// under "jucePluginState": a 4-byte magic, a 4-byte length, then the XML text.
CFPropertyListRef withCables(CFPropertyListRef state, const std::string& cables) {
  auto dict = CFDictionaryCreateMutableCopy(nullptr, 0, (CFDictionaryRef)state);
  auto data = (CFDataRef)CFDictionaryGetValue(dict, CFSTR("jucePluginState"));
  if (!data || CFDataGetLength(data) < 8) return dict;
  const auto* bytes = CFDataGetBytePtr(data);
  std::string xml((const char*)bytes + 8, strnlen((const char*)bytes + 8, (size_t)CFDataGetLength(data) - 8));
  const std::string key = "cables=\"";
  const size_t at = xml.find(key);
  if (at == std::string::npos) return dict;
  const size_t end = xml.find('"', at + key.size());
  xml.replace(at + key.size(), end - at - key.size(), cables);
  std::vector<uint8_t> out(bytes, bytes + 4);
  const uint32_t len = (uint32_t)xml.size() + 1;
  for (int i = 0; i < 4; i++) out.push_back((uint8_t)(len >> (8 * i)));
  out.insert(out.end(), xml.begin(), xml.end());
  out.push_back(0);
  CFDataRef newData = CFDataCreate(nullptr, out.data(), (CFIndex)out.size());
  CFDictionarySetValue(dict, CFSTR("jucePluginState"), newData);
  return dict;
}

double peak(const std::vector<float>& x) {
  double p = 0;
  for (float v : x) p = std::max(p, (double)std::abs(v));
  return p;
}

// Sample indices where a note starts (silence → sound).
std::vector<size_t> onsets(const std::vector<float>& x) {
  std::vector<size_t> at;
  size_t quiet = 0;
  for (size_t i = 0; i < x.size(); i++) {
    if (std::abs(x[i]) < 1e-4) quiet++;
    else {
      if (quiet > 200) at.push_back(i);
      quiet = 0;
    }
  }
  return at;
}

} // namespace

int main() {
  Instance a;
  if (!a.open()) {
    std::printf("Couldn't open the installed Catacomb AU (aumu Cat1 Ctcb). Run plugin/build.sh install first.\n");
    return 1;
  }
  a.setParam("EG2 (VCA) Decay", 0.1f); // short notes: clear gaps between them

  // Transport stopped: silent, and a MIDI note plays it like a keyboard.
  check(peak(a.render(0.3)) < 1e-6, "silent while the transport is stopped");
  MusicDeviceMIDIEvent(a.au, 0x90, 64, 100, 0);
  check(peak(a.render(0.2)) > 0.05, "a MIDI note plays it while stopped");
  a.render(0.5);

  // Transport playing from bar 1 at 120 BPM, 1/16 steps (6000 samples).
  a.transport.playing = true;
  a.transport.sample = 0;
  const auto played = a.render(4.0);
  const auto on = onsets(played);
  int offGrid = 0;
  for (size_t t : on) {
    const long step = 6000;
    const long d = (long)t % step;
    if (std::min(d, step - d) > 96) offGrid++; // > 2 ms from a 1/16 line
  }
  std::printf("      %zu notes in 4 s, %d off the 1/16 grid\n", on.size(), offGrid);
  check(on.size() >= 8, "the sequencer plays when Logic plays");
  check(offGrid == 0, "every note starts on Logic's 1/16 grid");

  // Logic playing, the sequencer running, all its bits off: a MIDI note (as from a
  // region or a Live Loop) still plays — the default MIDI Notes mode.
  {
    Instance k;
    k.open();
    k.transport.playing = true;
    k.render(0.2);
    MusicDeviceMIDIEvent(k.au, 0x90, 60, 100, 0);
    // The factory pattern may also be sounding; compare against no note.
    Instance quiet;
    quiet.open();
    quiet.transport.playing = true;
    quiet.render(0.2);
    const double withNote = peak(k.render(0.1)), without = peak(quiet.render(0.1));
    std::printf("      during playback: with a MIDI note %.3f, without %.3f\n", withNote, without);
    check(withNote > 0.05 && withNote > without + 0.02, "a MIDI note plays while Logic plays (MIDI Notes: Play)");
  }

  a.transport.playing = false;
  a.render(1.0); // let the last note die away
  check(peak(a.render(0.3)) < 1e-6, "stops with the transport");

  // Save, restore into a fresh instance, play both from bar 1 with heavy CORRUPT:
  // they must stay identical (same pattern, same random future).
  a.setParam("SEQ1 Corrupt", 0.9f);
  a.setParam("SEQ1 CV Range", 0.5f);
  a.setParam("VCO SEQ1 Amount", 1.0f);
  a.render(0.1);
  CFPropertyListRef saved = a.saveState();
  check(saved != nullptr, "saves its state (ClassInfo)");
  Instance b;
  b.open();
  check(b.loadState(saved), "restores the state into a new instance");
  b.render(0.1);
  a.transport = b.transport = {};
  a.transport.playing = b.transport.playing = true;
  const auto pa = a.render(4.0), pb = b.render(4.0);
  double diff = 0;
  for (size_t i = 0; i < pa.size(); i++) diff = std::max(diff, (double)std::abs(pa[i] - pb[i]));
  std::printf("      max difference between original and restored: %g (peak %.2f)\n", diff, peak(pa));
  check(peak(pa) > 0.05 && diff < 1e-6, "a restored project plays (and mutates) identically");

  // Cables travel in the saved state: MOD VCO → VCW VCA CV opens the VCA with no trigger.
  Instance c;
  c.open();
  c.setParam("MOD VCO Frequency", 0.55f);
  check(peak(c.render(0.3)) < 1e-6, "unpatched and untriggered: silent");
  CFPropertyListRef patched = withCables(c.saveState(), "mvco>vcwVcaCv");
  Instance d;
  d.open();
  check(d.loadState(patched), "loads a state with a cable in it");
  d.render(0.05);
  const double p = peak(d.render(0.5));
  std::printf("      peak with MOD VCO → VCW VCA: %.3f\n", p);
  check(p > 0.02, "the loaded cable is live");
  CFPropertyListRef resaved = d.saveState();
  auto data = (CFDataRef)CFDictionaryGetValue((CFDictionaryRef)resaved, CFSTR("jucePluginState"));
  const std::string xml((const char*)CFDataGetBytePtr(data) + 8, (size_t)CFDataGetLength(data) - 8);
  check(xml.find("cables=\"mvco&gt;vcwVcaCv\"") != std::string::npos, "and is saved again with the project");

  // Factory presets: listed for Logic's preset menu, and selecting one loads it all.
  {
    Instance f;
    f.open();
    CFArrayRef presets = nullptr;
    UInt32 size = sizeof presets;
    AudioUnitGetProperty(f.au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &presets, &size);
    const CFIndex count = presets ? CFArrayGetCount(presets) : 0;
    std::printf("      %ld factory presets\n", (long)count);
    check(count >= 10, "factory presets are listed for Logic");
    // "Bone Hats" patches CLOCK → VCO 1V/OCT; find it and select it like Logic would.
    for (CFIndex i = 0; i < count; i++) {
      auto* p = (AUPreset*)CFArrayGetValueAtIndex(presets, i);
      char name[128] = {};
      CFStringGetCString(p->presetName, name, sizeof name, kCFStringEncodingUTF8);
      if (std::string(name) != "Bone Hats") continue;
      AUPreset choice = *p;
      check(AudioUnitSetProperty(f.au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &choice, sizeof choice) == noErr,
            "selects a factory preset");
      f.transport.playing = true;
      check(peak(f.render(2.0)) > 0.05, "the preset plays");
      auto data = (CFDataRef)CFDictionaryGetValue((CFDictionaryRef)f.saveState(), CFSTR("jucePluginState"));
      const std::string xml((const char*)CFDataGetBytePtr(data) + 8, (size_t)CFDataGetLength(data) - 8);
      check(xml.find("clock&gt;vco1voct") != std::string::npos, "and brings its cables");
    }
  }

  std::printf("\n%s\n", failures ? "FAILED" : "all host checks passed");
  return failures ? 1 : 0;
}
