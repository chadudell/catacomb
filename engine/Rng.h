// Catacomb — the one source of randomness. Seeded and small enough to save with the
// project, so a reopened song (or an offline bounce) mutates exactly the same way.
#pragma once

#include <cstdint>

namespace catacomb {

// xoshiro128** (Blackman & Vigna), seeded through splitmix64.
class Rng {
public:
  explicit Rng(uint64_t seed = 0x5eed'ca7a'c0b0ULL) { reseed(seed); }

  void reseed(uint64_t seed) {
    for (auto& word : s) {
      seed += 0x9e3779b97f4a7c15ULL;
      uint64_t z = seed;
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
      word = (uint32_t)(z ^ (z >> 31));
    }
  }

  uint32_t next() {
    const uint32_t result = rotl(s[1] * 5, 7) * 9;
    const uint32_t t = s[1] << 9;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 11);
    return result;
  }

  // [0, 1)
  double uniform() { return (next() >> 8) * (1.0 / 16777216.0); }
  // [lo, hi)
  double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }
  bool chance(double p) { return uniform() < p; }

  // The full state, for saving and restoring.
  uint32_t s[4]{};

private:
  static uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
};

} // namespace catacomb
