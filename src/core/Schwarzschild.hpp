#pragma once

#include "core/Vec3.hpp"

// Schwarzschild (non-rotating) black hole in geometric units G = c = M = 1.
// Lengths are in units of M (GM/c^2), times in units of M/c.
//
// Formulation ("pseudo-Cartesian")
// --------------------------------
// Schwarzschild geodesics are planar, so we track x = r * n, where r is the
// Schwarzschild (areal) radius and n the unit direction, and v = dx/dλ with
// λ = proper time for massive particles or the affine parameter for photons.
// With h = |x × v| (specific angular momentum L) the geodesic equation reduces
// EXACTLY to the central-force law
//
//     d²x/dλ² = -M x / r³ · (κ + 3 h² / r²),    κ = 1 massive, κ = 0 photon.
//
// Its radial component reproduces d²r/dλ² = -κM/r² + h²/r³ - 3Mh²/r⁴ and its
// orbit equation reproduces u'' + u = κM/h² + 3Mu² (u = 1/r), i.e. the exact
// Schwarzschild equations. The equation is regular at r = 2M, so particles
// integrated in proper time fall through the horizon smoothly.
namespace bh::gr {

constexpr float kMass = 1.0f;
constexpr float kHorizon = 2.0f;                      // r_s = 2M
constexpr float kPhotonSphere = 3.0f;                 // unstable circular photon orbit
constexpr float kIsco = 6.0f;                         // innermost stable circular orbit
constexpr float kCriticalImpact = 5.196152422706632f; // 3√3 M: shadow radius seen from infinity

enum class Kind : unsigned { Massive = 0, Photon = 1 };

BH_HD BH_INLINE float kappaOf(Kind k) { return k == Kind::Photon ? 0.0f : 1.0f; }

// Lapse squared, 1 - 2M/r.
BH_HD BH_INLINE float lapse2(float r) { return 1.0f - 2.0f * kMass / r; }

BH_HD BH_INLINE float angularMomentum2(Vec3 x, Vec3 v) { return length2(cross(x, v)); }

// Geodesic acceleration d²x/dλ² (see header comment).
BH_HD BH_INLINE Vec3 acceleration(Vec3 x, float h2, float kappa) {
  const float r2 = dot(x, x);
  const float r = sqrtf(r2);
  return x * (-kMass / (r2 * r) * (kappa + 3.0f * h2 / r2));
}

// Conserved energy per unit rest mass (or photon energy at infinity, up to the
// arbitrary affine scale):  E² = ṙ² + (1 - 2M/r)(κ + h²/r²).
BH_HD BH_INLINE float energy(Vec3 x, Vec3 v, float kappa) {
  const float r2 = dot(x, x);
  const float r = sqrtf(r2);
  const float rdot = dot(x, v) / r;
  const float h2 = angularMomentum2(x, v);
  const float e2 = rdot * rdot + lapse2(r) * (kappa + h2 / r2);
  return sqrtf(fmaxf(e2, 0.0f));
}

// Circular-orbit speed |dx/dτ| = L/r = sqrt(M / (r - 3M)), valid for r > 3M.
BH_HD BH_INLINE float circularSpeed(float r) { return sqrtf(kMass / (r - 3.0f * kMass)); }

// Kinematics of a massive particle as seen by a local static observer.
struct LocalKinematics {
  Vec3 beta;           // 3-velocity in the static observer's orthonormal frame (|beta| < 1)
  float timeDilation;  // dτ/dt = (1 - 2M/r) / E: how fast the particle's clock ticks vs. a clock at infinity
};

// β_r = ṙ/E,  β_⊥ = sqrt(1 - 2M/r) v_⊥ / E   (from u^t = E/(1-2M/r) and the static tetrad).
BH_HD BH_INLINE LocalKinematics localKinematics(Vec3 x, Vec3 v) {
  const float r = length(x);
  const Vec3 n = x / r;
  const float rdot = dot(n, v);
  const Vec3 vPerp = v - n * rdot;
  const float f = lapse2(r);
  const float e = fmaxf(energy(x, v, 1.0f), 1e-6f);
  LocalKinematics k;
  k.beta = (n * rdot + vPerp * sqrtf(fmaxf(f, 0.0f))) / e;
  k.timeDilation = f / e;
  return k;
}

// Redshift factor g = ν_observed / ν_emitted for a photon that leaves the
// emitter in (local static-frame) direction `dir` and reaches an observer at
// rest at infinity:  g = (1 - 2M/r) / (E (1 - β·dir)) = timeDilation / (1 - β·dir).
// Includes gravitational redshift, transverse and longitudinal Doppler.
BH_HD BH_INLINE float redshiftFactor(const LocalKinematics& k, Vec3 dir) {
  return k.timeDilation / (1.0f - dot(k.beta, dir));
}

}  // namespace bh::gr
