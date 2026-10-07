#include "StateText.h"

#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <vector>

namespace catacomb {

namespace {

constexpr const char* kHeader = "catacomb-seq=1";

long long microvolts(float v) { return std::llround((double)v * 1e6); }
float fromMicrovolts(long long uv) { return (float)((double)uv / 1e6); }

template <typename T, typename F>
void line(std::ostringstream& out, const char* key, const T* v, int n, F conv) {
  out << key << '=';
  for (int i = 0; i < n; i++) out << (i ? "," : "") << conv(v[i]);
  out << '\n';
}

void encodeMemory(std::ostringstream& out, const char* p, const SeqMemory& m) {
  const std::string k(p);
  bool on[kCells];
  float volts[kCells];
  for (int c = 0; c < kCells; c++) {
    on[c] = m.cells[c].on;
    volts[c] = m.cells[c].volts;
  }
  line(out, (k + ".on").c_str(), on, kCells, [](bool b) { return b ? 1 : 0; });
  line(out, (k + ".uv").c_str(), volts, kCells, microvolts);
  line(out, (k + ".origin").c_str(), m.origin, kCells, [](uint8_t o) { return (int)o; });
  line(out, (k + ".length").c_str(), m.length, 2, [](int v) { return v; });
  line(out, (k + ".offset").c_str(), m.writeOffset, 2, [](int v) { return v; });
  out << k << ".quant=" << m.quantMode << '\n';
  out << k << ".chain=" << (m.chained ? 1 : 0) << '\n';
}

using Fields = std::map<std::string, std::vector<long long>>;

// Copies up to n values of `key` (if present) through conv.
template <typename T, typename F>
void read(const Fields& f, const std::string& key, T* dest, int n, F conv) {
  const auto it = f.find(key);
  if (it == f.end()) return;
  for (int i = 0; i < n && i < (int)it->second.size(); i++) dest[i] = conv(it->second[(size_t)i]);
}

void decodeMemory(const Fields& f, const std::string& k, SeqMemory& m) {
  bool on[kCells];
  float volts[kCells];
  for (int c = 0; c < kCells; c++) {
    on[c] = m.cells[c].on;
    volts[c] = m.cells[c].volts;
  }
  read(f, k + ".on", on, kCells, [](long long v) { return v != 0; });
  read(f, k + ".uv", volts, kCells, [](long long v) { return std::fmax(-5.0f, std::fmin(5.0f, fromMicrovolts(v))); });
  for (int c = 0; c < kCells; c++) m.cells[c] = {on[c], volts[c]};
  read(f, k + ".origin", m.origin, kCells, [](long long v) { return (uint8_t)v; });
  read(f, k + ".length", m.length, 2, [](long long v) { return (int)v; });
  read(f, k + ".offset", m.writeOffset, 2, [](long long v) { return (int)v; });
  read(f, k + ".quant", &m.quantMode, 1, [](long long v) { return (int)v; });
  read(f, k + ".chain", &m.chained, 1, [](long long v) { return v != 0; });
}

} // namespace

std::string encodeSequencer(const Sequencer::State& s) {
  std::ostringstream out;
  out << kHeader << '\n';
  encodeMemory(out, "mem", s.mem);
  encodeMemory(out, "buf", s.saved);
  out << "buf.valid=" << (s.bufferValid ? 1 : 0) << '\n';
  line(out, "pos", s.pos, 2, [](int v) { return v; });
  line(out, "held.uv", s.held, 2, microvolts);
  out << "running=" << (s.running ? 1 : 0) << '\n';
  out << "root=" << s.rootSemis << '\n';
  line(out, "rng", s.rng, 4, [](uint32_t v) { return (unsigned long long)v; });
  return out.str();
}

bool decodeSequencer(const std::string& text, Sequencer::State& out) {
  std::istringstream in(text);
  std::string ln;
  if (!std::getline(in, ln) || ln != kHeader) return false;

  Fields f;
  while (std::getline(in, ln)) {
    const auto eq = ln.find('=');
    if (eq == std::string::npos) continue;
    std::vector<long long> values;
    const char* p = ln.c_str() + eq + 1;
    while (*p) {
      char* end = nullptr;
      const long long v = std::strtoll(p, &end, 10); // integers: locale-independent
      if (end == p) break;
      values.push_back(v);
      p = *end == ',' ? end + 1 : end;
    }
    f[ln.substr(0, eq)] = std::move(values);
  }

  Sequencer::State s = out;
  decodeMemory(f, "mem", s.mem);
  decodeMemory(f, "buf", s.saved);
  read(f, "buf.valid", &s.bufferValid, 1, [](long long v) { return v != 0; });
  read(f, "pos", s.pos, 2, [](long long v) { return (int)v; });
  read(f, "held.uv", s.held, 2, [](long long v) { return fromMicrovolts(v); });
  read(f, "running", &s.running, 1, [](long long v) { return v != 0; });
  read(f, "root", &s.rootSemis, 1, [](long long v) { return (int)v; });
  read(f, "rng", s.rng, 4, [](long long v) { return (uint32_t)v; });
  out = s;
  return true; // Sequencer::setState() clamps anything out of range
}

std::string encodeCables(const std::vector<Cable>& cables) {
  std::string out;
  for (const auto& c : cables) {
    if (!out.empty()) out += ',';
    out += kOutNames[(int)c.out];
    out += '>';
    out += kInNames[(int)c.in];
  }
  return out;
}

std::vector<Cable> decodeCables(const std::string& text) {
  std::vector<Cable> cables;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find(',', start);
    if (end == std::string::npos) end = text.size();
    const std::string item = text.substr(start, end - start);
    const size_t gt = item.find('>');
    if (gt != std::string::npos) {
      const int o = outByName(item.substr(0, gt).c_str());
      const int i = inByName(item.substr(gt + 1).c_str());
      bool dup = false;
      for (const auto& c : cables) dup |= (int)c.out == o && (int)c.in == i;
      if (o >= 0 && i >= 0 && !dup) cables.push_back({(Out)o, (In)i});
    }
    start = end + 1;
  }
  return cables;
}

Patch patchFrom(const std::vector<Cable>& cables) {
  Patch p;
  for (const auto& c : cables) p.connect(c.out, c.in);
  return p;
}

} // namespace catacomb
