// Catacomb — the sequencer's quantizer: 16 modes (manual p. 38), 1 V/octave.
#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace catacomb {

struct Scale {
  const char* name;
  uint16_t mask; // bit n set = pitch class n (semitones above the root) is in the scale
};

constexpr uint16_t pcs(std::initializer_list<int> notes) {
  uint16_t m = 0;
  for (int n : notes) m = (uint16_t)(m | (1u << (n % 12)));
  return m;
}

constexpr int kNumScales = 16;

// Index 0 is "Unquantized" (mask unused). Two of these the manual names without
// spelling out: Diminished 6th (Barry Harris: major scale + #5) and Hang drum (an
// "Integral"-style hang: root, 2, b3, 5, b6, b7) — our choices.
constexpr Scale kScales[kNumScales] = {
    {"Unquantized", 0},
    {"Chromatic", 0x0fff},
    {"Major", pcs({0, 2, 4, 5, 7, 9, 11})},
    {"Pentatonic", pcs({0, 2, 4, 7, 9})},
    {"Melodic Minor", pcs({0, 2, 3, 5, 7, 9, 11})},
    {"Harmonic Minor", pcs({0, 2, 3, 5, 7, 8, 11})},
    {"Diminished 6th", pcs({0, 2, 4, 5, 7, 8, 9, 11})},
    {"Whole Tone", pcs({0, 2, 4, 6, 8, 10})},
    {"Hirajoshi Pentatonic", pcs({0, 2, 3, 7, 8})},
    {"7 Sus 4 (1 4 5 b7)", pcs({0, 5, 7, 10})},
    {"Major 7th (1 3 5 7)", pcs({0, 4, 7, 11})},
    {"Major 13th (1 3 5 6 7 9)", pcs({0, 4, 7, 9, 11, 14})},
    {"Minor 7th (1 b3 5 b7)", pcs({0, 3, 7, 10})},
    {"Minor 11th (1 b3 4 5 b7 9)", pcs({0, 3, 5, 7, 10, 14})},
    {"Hang Drum", pcs({0, 2, 3, 7, 8, 10})},
    {"Quads (Minor 3rds)", pcs({0, 3, 6, 9})},
};

constexpr int kScaleUnquantized = 0;
constexpr int kScaleMajor = 2; // the factory setting

// volts → volts. `rootSemis` transposes the scale (and the output) by whole semitones;
// it only applies when quantizing, as on the hardware (MIDI notes move the root only
// when the quantizer is on).
inline double quantize(double volts, int scale, int rootSemis = 0) {
  if (scale <= kScaleUnquantized || scale >= kNumScales) return volts;
  const uint16_t mask = kScales[scale].mask;
  const double semis = volts * 12.0;
  // Search outward from the nearest semitone; the scale repeats every octave so a
  // note is always within 6 semitones. Ties go to the lower note.
  const int centre = (int)std::floor(semis + 0.5);
  int best = centre;
  double bestDist = 1e9;
  for (int d = -7; d <= 7; d++) {
    const int n = centre + d;
    const int pc = ((n % 12) + 12) % 12;
    if (!(mask & (1u << pc))) continue;
    const double dist = std::abs(n - semis);
    if (dist < bestDist - 1e-12) {
      bestDist = dist;
      best = n;
    }
  }
  return (best + rootSemis) / 12.0;
}

} // namespace catacomb
