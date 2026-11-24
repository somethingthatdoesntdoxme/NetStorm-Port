#pragma once
// Deterministic maths for the simulation. std::sin/cos/atan2 can differ in the
// last bit between C libraries (glibc vs MSVC), which would desync a mixed
// Windows/Linux lockstep game. These use only + - * / and comparisons, which
// IEEE-754 makes identical everywhere (build with FMA contraction off).
#include <cstdint>

namespace ns::det {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;

// Wrap to [-pi, pi).
inline float wrap(float a) {
  float k = a / kTwoPi;
  // floor without libm: truncate, adjust for negatives.
  float t = float(int32_t(k));
  if (t > k) t -= 1.0f;
  a -= t * kTwoPi;           // now in [0, 2pi)
  if (a >= kPi) a -= kTwoPi;  // [-pi, pi)
  return a;
}

// sin on [-pi, pi): fold to [-pi/2, pi/2], then a 9th-order odd polynomial (|err| < 2e-6).
inline float sin(float x) {
  x = wrap(x);
  if (x > kPi / 2) x = kPi - x;
  else if (x < -kPi / 2) x = -kPi - x;
  float x2 = x * x;
  return x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880)))));
}

inline float cos(float x) { return det::sin(x + kPi / 2); }

// atan on [-1, 1]: minimax-style polynomial (|err| < 1e-5 rad).
inline float atanUnit(float z) {
  float z2 = z * z;
  return z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f + z2 * (-0.11643287f + z2 * (0.05265332f +
                                                                                                 z2 * -0.01172120f)))));
}

inline float atan2(float y, float x) {
  if (x == 0 && y == 0) return 0;
  float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
  float a = ax >= ay ? atanUnit(ay / ax) : kPi / 2 - atanUnit(ax / ay);
  if (x < 0) a = kPi - a;
  return y < 0 ? -a : a;
}

// The signed angle difference a - b wrapped to [-pi, pi).
inline float angleDiff(float a, float b) { return wrap(a - b); }

}  // namespace ns::det
