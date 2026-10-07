// Catacomb — small DSP building blocks used by the voice. All real-time safe.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace catacomb::dsp {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2 * kPi;

// ---- Saturation --------------------------------------------------------------------------

// Clean up to `knee`, then bends smoothly toward knee + headroom (C1-continuous).
// The mixer's channels overdrive like this once a knob passes noon.
inline double softKnee(double x, double knee, double headroom) {
  const double a = std::abs(x);
  if (a <= knee) return x;
  const double y = knee + headroom * std::tanh((a - knee) / headroom);
  return x < 0 ? -y : y;
}

// Symmetric tanh saturation that's unity-gain for small signals.
inline double saturate(double x, double ceiling) { return ceiling * std::tanh(x / ceiling); }

// ---- Filters -------------------------------------------------------------------------------

struct OnePoleLP {
  double y = 0;
  double process(double x, double a) { return y += a * (x - y); }
  static double coef(double hz, double fs) { return 1.0 - std::exp(-kTwoPi * hz / fs); }
};

struct DcBlocker {
  double x1 = 0, y1 = 0, r = 0.9995;
  void prepare(double fs, double hz = 8.0) { r = std::exp(-kTwoPi * hz / fs); }
  double process(double x) {
    const double y = x - x1 + r * y1;
    x1 = x;
    y1 = y;
    return y;
  }
};

// Two-pole state-variable filter, trapezoidal (Simper/Zavalishin). `k` is damping:
// 2 = no resonance, → 0 = self-oscillation. A soft limit on the band state keeps
// high resonance bounded and adds a little analog-style compression.
struct Svf {
  double ic1 = 0, ic2 = 0;
  double lp = 0, bp = 0;

  void process(double x, double g, double k) {
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double a2 = g * a1;
    const double a3 = g * a2;
    const double v3 = x - ic2;
    const double v1 = a1 * ic1 + a2 * v3;
    const double v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = softKnee(2 * v1 - ic1, 8.0, 6.0);
    ic2 = 2 * v2 - ic2;
    bp = v1;
    lp = v2;
  }
  static double gain(double hz, double fs) { return std::tan(kPi * std::min(hz, 0.45 * fs) / fs); }
  void reset() { ic1 = ic2 = lp = bp = 0; }
};

// ---- Envelope ------------------------------------------------------------------------------

// Decay-only envelope, 0…8 V. A trigger ramps to 8 V × velocity in about half a
// millisecond (so retriggers don't click), then it decays exponentially.
struct DecayEnvelope {
  double level = 0, target = 0;
  bool attacking = false;

  void trigger(double velocity) {
    target = 8.0 * std::clamp(velocity, 0.0, 1.0);
    attacking = true;
  }
  // step: volts per sample during the attack ramp; decay: per-sample multiplier.
  double tick(double step, double decay) {
    if (attacking) {
      if (std::abs(target - level) <= step) {
        level = target;
        attacking = false;
      } else {
        level += target > level ? step : -step;
      }
    } else {
      level *= decay;
      if (level < 1e-6) level = 0;
    }
    return level;
  }
};

// ---- Half-band resampling ------------------------------------------------------------------

// Polyphase IIR half-band filters (two allpass chains; Laurent de Soras' design, as in
// his HIIR library). Low latency, no pre-ringing. The coefficients come from the
// transition bandwidth.
constexpr int kHalfbandCoefs = 10;

inline void designHalfband(double transition, double* coef) {
  // Elliptic-filter parameters from the transition bandwidth.
  double k = std::tan((1 - transition * 2) * kPi / 4);
  k *= k;
  const double kksqrt = std::pow(1 - k * k, 0.25);
  const double e = 0.5 * (1 - kksqrt) / (1 + kksqrt);
  const double e4 = e * e * e * e;
  const double q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)));
  const int order = kHalfbandCoefs * 2 + 1;
  for (int i = 0; i < kHalfbandCoefs; i++) {
    const int c = i + 1;
    double num = 0, den = 0;
    for (int n = 0, sign = 1; n < 64; n++, sign = -sign) {
      const double t = std::pow(q, n * (n + 1)) * std::sin((n * 2 + 1) * c * kPi / order) * sign;
      num += t;
      if (std::abs(t) < 1e-100) break;
    }
    for (int n = 1, sign = -1; n < 64; n++, sign = -sign) {
      const double t = std::pow(q, n * n) * std::cos(n * 2 * c * kPi / order) * sign;
      den += t;
      if (std::abs(t) < 1e-100) break;
    }
    const double ww = num * std::pow(q, 0.25) / (den + 0.5);
    const double wwsq = ww * ww;
    const double r = std::sqrt((1 - wwsq * k) * (1 - wwsq / k)) / (1 + wwsq);
    coef[i] = (1 - r) / (1 + r);
  }
}

class HalfbandChains {
public:
  void design(double transition) {
    designHalfband(transition, coef);
    reset();
  }
  void reset() {
    std::fill(std::begin(x), std::end(x), 0.0);
    std::fill(std::begin(y), std::end(y), 0.0);
  }
  double coef[kHalfbandCoefs]{};

protected:
  // Runs `a` through the even-indexed allpasses and `b` through the odd ones.
  void run(double& a, double& b) {
    for (int i = 0; i < kHalfbandCoefs; i += 2) {
      a = stage(i, a);
      b = stage(i + 1, b);
    }
  }

private:
  double stage(int i, double in) {
    const double out = (in - y[i]) * coef[i] + x[i];
    x[i] = in;
    y[i] = out;
    return out;
  }
  double x[kHalfbandCoefs]{}, y[kHalfbandCoefs]{};
};

// Two input samples (in time order) → one output sample at half the rate.
class Downsampler2x : public HalfbandChains {
public:
  double process(double first, double second) {
    double a = second, b = first; // path 0 takes the later sample, path 1 the earlier
    run(a, b);
    return 0.5 * (a + b);
  }
};

// One input sample → two output samples (in time order) at twice the rate.
class Upsampler2x : public HalfbandChains {
public:
  void process(double in, double& first, double& second) {
    double a = in, b = in;
    run(a, b);
    first = a;
    second = b;
  }
};

// ---- Noise ---------------------------------------------------------------------------------

// Fast white noise for audio (separate from the sequencer's saved RNG).
struct WhiteNoise {
  uint32_t s = 0x9e3779b9u;
  double next() { // roughly Gaussian (sum of two uniforms), ±1 peak, ~0.41 rms
    return 0.5 * (uniform() + uniform());
  }
  double uniform() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (double)s * (2.0 / 4294967296.0) - 1.0;
  }
};

} // namespace catacomb::dsp
