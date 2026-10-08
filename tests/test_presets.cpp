#include "check.h"

#include "../engine/Divisions.h"
#include "../engine/Engine.h"
#include "../engine/Presets.h"
#include "../engine/StateText.h"

#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace catacomb;

namespace {

std::vector<float> renderPreset(const Preset& preset, double seconds) {
  Engine e;
  e.params = presetParams(preset);
  e.prepare(48000);
  e.seq.setState(presetSequencer(preset, e.seq.state()));
  e.patch = patchFrom(decodeCables(preset.cables));
  e.setClockRates(2.0 / kDivisions[preset.clockDiv].beats, 0);
  e.seq.setRunning(true);
  std::vector<float> out((size_t)(seconds * 48000));
  for (size_t i = 0; i < out.size(); i += 512) e.process(out.data() + i, (int)std::min<size_t>(512, out.size() - i));
  return out;
}

} // namespace

TEST("presets: unique names, valid ids, scales, cables and bits") {
  std::set<std::string> names;
  for (const auto& p : factoryPresets()) {
    CHECK(names.insert(p.name).second);
    for (const auto& [id, value] : p.params) {
      bool known = false;
      for (int i = 0; i < kNumParams; i++)
        if (std::strcmp(paramInfo((Param)i).id, id) == 0) {
          known = true;
          CHECK(value >= paramInfo((Param)i).min && value <= paramInfo((Param)i).max);
        }
      CHECK(known);
    }
    CHECK(p.scale >= 0 && p.scale < kNumScales);
    CHECK(p.clockDiv >= 0 && p.clockDiv < 16);
    CHECK(std::strlen(p.bits1) == 8 && std::strlen(p.bits2) == 8);
    CHECK(encodeCables(decodeCables(p.cables)) == p.cables); // every cable parses
  }
  CHECK(factoryPresets().size() >= 10);
}

TEST("presets: every one plays, finite and under the limiter, and starts the same way twice") {
  for (const auto& p : factoryPresets()) {
    const auto a = renderPreset(p, 4.0);
    double sum = 0;
    float peak = 0;
    bool finite = true;
    for (float v : a) {
      finite &= std::isfinite(v);
      peak = std::max(peak, std::abs(v));
      sum += (double)v * v;
    }
    const double rms = std::sqrt(sum / (double)a.size());
    if (!(finite && rms > 0.01 && peak <= (float)Engine::kLimiterCeiling + 1e-4f))
      std::printf("      %s: rms %.4f peak %.3f\n", p.name, rms, peak);
    CHECK(finite);
    CHECK(rms > 0.01);
    CHECK(peak <= (float)Engine::kLimiterCeiling + 1e-4f);
    CHECK(renderPreset(p, 1.0) == std::vector<float>(a.begin(), a.begin() + 48000));
  }
}
