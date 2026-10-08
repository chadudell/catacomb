// Renders every factory preset to WAV (renders/*.wav, 12 s at 120 BPM) and reports level,
// silence and CPU use, so the engine can be heard without the plugin. Build and run: tests/render.sh
#include "../engine/Divisions.h"
#include "../engine/Engine.h"
#include "../engine/Presets.h"
#include "../engine/StateText.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace catacomb;

namespace {

constexpr double kFs = 48000;

void writeWav(const std::string& path, const std::vector<float>& x) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  const uint32_t bytes = (uint32_t)x.size() * 2;
  std::fwrite("RIFF", 1, 4, f);
  u32(36 + bytes);
  std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16), u16(1), u16(1), u32((uint32_t)kFs), u32((uint32_t)kFs * 2), u16(2), u16(16);
  std::fwrite("data", 1, 4, f);
  u32(bytes);
  for (float v : x) {
    const int16_t s = (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767);
    std::fwrite(&s, 2, 1, f);
  }
  std::fclose(f);
}

} // namespace

int main() {
  std::system("rm -rf renders && mkdir -p renders");
  int n = 0;
  for (const auto& preset : factoryPresets()) {
    Engine e;
    e.params = presetParams(preset);
    e.prepare(kFs);
    e.seq.setState(presetSequencer(preset, e.seq.state()));
    e.patch = patchFrom(decodeCables(preset.cables));
    e.setClockRates(120.0 / 60.0 / kDivisions[preset.clockDiv].beats, 0); // 120 BPM
    e.seq.setRunning(true);

    std::vector<float> out((size_t)(12 * kFs));
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < out.size(); i += 256) e.process(out.data() + i, 256);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    float peak = 0;
    double sum = 0;
    int silent = 0, windows = 0;
    for (size_t i = 0; i + 4800 <= out.size(); i += 4800, windows++) {
      float wp = 0;
      for (size_t j = i; j < i + 4800; j++) wp = std::max(wp, std::abs(out[j]));
      silent += wp < 1e-3f;
    }
    for (float v : out) {
      peak = std::max(peak, std::abs(v));
      sum += (double)v * v;
    }
    char name[128];
    std::snprintf(name, sizeof name, "renders/%02d %s.wav", ++n, preset.name);
    writeWav(name, out);
    std::printf("%-24s peak %5.2f  rms %5.3f  silent %3d%%  CPU %4.1f%%\n", preset.name, peak,
                std::sqrt(sum / (double)out.size()), 100 * silent / windows, 100 * secs / 12.0);
  }
}
