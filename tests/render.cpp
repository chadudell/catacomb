// Renders a few example patches to WAV (renders/*.wav) and reports CPU use, so the
// engine can be heard without the plugin. Build and run: tests/render.sh
#include "../engine/Engine.h"

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

double knobFor(double hz, double lo, double hi) { return std::log(hz / lo) / std::log(hi / lo); }

struct Patch {
  const char* name;
  std::function<void(Engine&)> set;
};

const Patch kPatches[] = {
    {"01-home-melody", [](Engine& e) {
       // The manual's first walkthrough: a sine VCO playing SEQ1, quantized to major.
       e.params[Param::VcoFreq] = (float)knobFor(262, 20, 5000);
       e.params[Param::VcoSeq1Amt] = 1;
       e.params[Param::CvRange1] = 0.35f;
       e.params[Param::Eg2Decay] = 0.4f;
     }},
    {"02-kick-and-bell", [](Engine& e) {
       // MOD VCO as a kick on SEQ2's rhythm, the VCO as a folded bell on SEQ1.
       e.params[Param::MvcoFreq] = (float)knobFor(48, 0.05, 1300);
       e.params[Param::MvcoEg1Amt] = 0.35f;
       e.params[Param::MvcoLvl] = 0.6f;
       e.params[Param::VcoFreq] = (float)knobFor(523, 20, 5000);
       e.params[Param::VcoSeq1Amt] = 1;
       e.params[Param::CvRange1] = 0.3f;
       e.params[Param::VcoLvl] = 0.35f;
       e.params[Param::Fold] = 0.35f;
       e.params[Param::FoldEg1Amt] = 0.4f;
       e.params[Param::Eg1Decay] = 0.25f;
       e.params[Param::Eg2Decay] = 0.35f;
       e.params[Param::EgTrigMix] = 0.5f;
       e.seq.setQuantMode(12); // minor 7th
     }},
    {"03-filter-sweep", [](Engine& e) {
       // The VCF path with everything in the mixer, resonant, EG1 on the cutoff.
       e.params[Param::Blend] = 1;
       e.params[Param::VcoLvl] = 0.8f;
       e.params[Param::MvcoLvl] = 0.6f;
       e.params[Param::RingLvl] = 0.5f;
       e.params[Param::NoiseLvl] = 0.2f;
       e.params[Param::MvcoFreq] = (float)knobFor(55, 0.05, 1300);
       e.params[Param::VcoFreq] = (float)knobFor(110, 20, 5000);
       e.params[Param::VcoSeq1Amt] = 1;
       e.params[Param::CvRange1] = 0.25f;
       e.params[Param::Cutoff] = 0.3f;
       e.params[Param::CutoffEg1Amt] = 0.55f;
       e.params[Param::CutoffSeq2Amt] = 0.4f;
       e.params[Param::CvRange2] = 0.4f;
       e.params[Param::Resonance] = 0.8f;
       e.params[Param::Eg1Decay] = 0.3f;
       e.params[Param::Eg2Decay] = 0.4f;
       e.seq.setQuantMode(1); // chromatic
     }},
    {"04-fm-fold-corrupt", [](Engine& e) {
       // Thru-zero FM into the folder, both sequencers mutating, VCF→VCW.
       e.params[Param::Order] = OrderVcfVcw;
       e.params[Param::FmAmt] = 0.35f;
       e.params[Param::MvcoFreq] = (float)knobFor(330, 0.05, 1300);
       e.params[Param::MvcoSeq2Amt] = 1;
       e.params[Param::CvRange2] = 0.3f;
       e.params[Param::VcoFreq] = (float)knobFor(220, 20, 5000);
       e.params[Param::VcoSeq1Amt] = 1;
       e.params[Param::CvRange1] = 0.3f;
       e.params[Param::Fold] = 0.3f;
       e.params[Param::FoldSeq1Amt] = 0.5f;
       e.params[Param::Bias] = 0.25f;
       e.params[Param::Cutoff] = 0.6f;
       e.params[Param::FilterMode] = 0.6f;
       e.params[Param::Resonance] = 0.5f;
       e.params[Param::Blend] = 0.3f;
       e.params[Param::Corrupt1] = 0.6f;
       e.params[Param::Corrupt2] = 0.4f;
       e.params[Param::EgTrigMix] = 0.5f;
       e.params[Param::Eg2Decay] = 0.3f;
       e.seq.setQuantMode(14); // hang drum
     }},
};

} // namespace

int main() {
  std::system("mkdir -p renders");
  for (const auto& p : kPatches) {
    Engine e;
    e.prepare(kFs);
    e.seq.factoryPattern();
    p.set(e);
    e.seq.setRunning(true);
    std::vector<float> out((size_t)(10 * kFs));
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < out.size(); i += 256) e.process(out.data() + i, 256);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    float peak = 0;
    for (float v : out) peak = std::max(peak, std::abs(v));
    writeWav(std::string("renders/") + p.name + ".wav", out);
    std::printf("%-22s peak %5.2f   CPU %4.1f%% of one core\n", p.name, peak, 100 * secs / 10.0);
  }
}
