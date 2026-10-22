#pragma once

#include "core/Vec3.hpp"

// Kerr (rotating) black hole in Cartesian Kerr-Schild coordinates, G = c = M = 1,
// spin a along +Z. Here (X, Y, Z) are the *formula* axes; the renderer maps
// them to its Y-up world as X = z, Y = x, Z = y (right-handed).
//
//   g^{μν} = η^{μν} - f l^μ l^ν,   f = 2 r³ / (r⁴ + a² Z²),
//   l_μ = (1, (rX + aY)/(r² + a²), (rY - aX)/(r² + a²), Z/r),
//   r⁴ - (R² - a²) r² - a² Z² = 0   (Boyer-Lindquist r).
//
// Geodesics follow from H = ½ g^{μν} p_μ p_ν with p_t = -E fixed (stationary):
//   dX/dλ = p - f (l·p) l,   dp/dλ = ½ ∇f (l·p)² + f (l·p) ∇(l·p),
// with l·p = l^μ p_μ = E + l⃗·p⃗. The coordinates are regular at the horizon.
// For a = 0 this is Schwarzschild in ingoing Eddington-Finkelstein form.
namespace bh::kerr {

BH_HD BH_INLINE float radius(Vec3 X, float a) {
  const float b = dot(X, X) - a * a;
  return sqrtf(0.5f * (b + sqrtf(b * b + 4.0f * a * a * X.z * X.z)));
}

BH_HD BH_INLINE float horizon(float a) { return 1.0f + sqrtf(fmaxf(1.0f - a * a, 0.0f)); }

// Prograde innermost stable circular orbit (Bardeen, Press & Teukolsky 1972); a < 0 = retrograde.
BH_HD inline float isco(float a) {
  const float z1 = 1.0f + cbrtf(1.0f - a * a) * (cbrtf(1.0f + a) + cbrtf(1.0f - a));
  const float z2 = sqrtf(3.0f * a * a + z1 * z1);
  const float s = a >= 0.0f ? 1.0f : -1.0f;
  return 3.0f + z2 - s * sqrtf((3.0f - z1) * (3.0f + z1 + 2.0f * z2));
}

struct Field {
  float r, f;
  Vec3 l;
};

BH_HD BH_INLINE Field field(Vec3 X, float a) {
  Field k;
  k.r = radius(X, a);
  const float r2 = k.r * k.r;
  const float q = r2 + a * a;
  k.f = 2.0f * r2 * k.r / (r2 * r2 + a * a * X.z * X.z);
  k.l = Vec3{(k.r * X.x + a * X.y) / q, (k.r * X.y - a * X.x) / q, X.z / k.r};
  return k;
}

// Hamilton's equations for (X, p) with p_t = -energy.
BH_HD inline void derivatives(Vec3 X, Vec3 p, float energy, float a, Vec3& dX, Vec3& dp) {
  const float r = radius(X, a);
  const float r2 = r * r, a2 = a * a, Z = X.z;
  const float q = r2 + a2;
  const float sigma = r2 * r2 + a2 * Z * Z;
  const float f = 2.0f * r2 * r / sigma;
  const Vec3 l{(r * X.x + a * X.y) / q, (r * X.y - a * X.x) / q, Z / r};
  const float lp = energy + dot(l, p);
  dX = p - l * (f * lp);

  // Gradients via implicit differentiation of the r(X) quartic: ∂r = (r² X, r² Y, (r² + a²) Z) r / Σ'.
  const Vec3 dr = Vec3{r2 * X.x, r2 * X.y, q * Z} * (r / sigma);
  const Vec3 df = (dr * (3.0f / r) - (dr * (4.0f * r2 * r) + Vec3{0.0f, 0.0f, 2.0f * a2 * Z}) / sigma) * f;
  const float nX = r * X.x + a * X.y, nY = r * X.y - a * X.x;
  const Vec3 dq = dr * (2.0f * r);
  const Vec3 dlX = ((dr * X.x + Vec3{r, a, 0.0f}) * q - dq * nX) / (q * q);
  const Vec3 dlY = ((dr * X.y + Vec3{-a, r, 0.0f}) * q - dq * nY) / (q * q);
  const Vec3 dlZ = (Vec3{0.0f, 0.0f, r} - dr * Z) / r2;
  const Vec3 dlp = dlX * p.x + dlY * p.y + dlZ * p.z;
  dp = df * (0.5f * lp * lp) + dlp * (f * lp);
}

// Spatial momentum along `dir` that makes a photon null for p_t = -1:
// k²(1 - f (l·n)²) - 2 f (l·n) k - (1 + f) = 0.
BH_HD inline Vec3 nullMomentum(Vec3 X, Vec3 dir, float a) {
  const Field k = field(X, a);
  const float ln = dot(k.l, dir);
  const float A = 1.0f - k.f * ln * ln;
  const float B = -2.0f * k.f * ln;
  const float C = -(1.0f + k.f);
  return dir * ((-B + sqrtf(B * B - 4.0f * A * C)) / (2.0f * A));
}

}  // namespace bh::kerr
