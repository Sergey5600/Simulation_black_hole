#pragma once

#include <cmath>

#include "core/Config.hpp"

namespace bh {

// Minimal float3 that works on host and device. We deliberately use float:
// consumer GPUs (e.g. RTX 3060) run FP64 at 1/64 of FP32 speed.
struct Vec3 {
  float x, y, z;
};

BH_HD BH_INLINE Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
BH_HD BH_INLINE Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
BH_HD BH_INLINE Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
BH_HD BH_INLINE Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
BH_HD BH_INLINE Vec3 operator*(float s, Vec3 a) { return {a.x * s, a.y * s, a.z * s}; }
BH_HD BH_INLINE Vec3 operator/(Vec3 a, float s) { return a * (1.0f / s); }
BH_HD BH_INLINE Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
BH_HD BH_INLINE Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }

BH_HD BH_INLINE float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
BH_HD BH_INLINE Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
BH_HD BH_INLINE float length2(Vec3 a) { return dot(a, a); }
BH_HD BH_INLINE float length(Vec3 a) { return sqrtf(dot(a, a)); }
BH_HD BH_INLINE Vec3 normalize(Vec3 a) {
  const float l2 = dot(a, a);
  return l2 > 0.0f ? a * (1.0f / sqrtf(l2)) : Vec3{0.0f, 0.0f, 0.0f};
}

}  // namespace bh
