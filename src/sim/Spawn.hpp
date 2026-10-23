#pragma once

#include "core/Random.hpp"
#include "sim/Particle.hpp"

// Initial conditions for every scenario, written as plain host/device functions
// so they can run inside a CUDA kernel later (no virtual calls, POD params).
namespace bh {

enum class ScenarioType : int { AccretionDisk = 0, PhotonWaves = 1, StarDisruption = 2 };

struct DiskParams {
  float innerRadius = gr::kIsco;
  float outerRadius = 40.0f;
  float thickness = 0.03f;        // vertical scale height h/r
  float eccentricity = 0.04f;     // spread of orbital speeds around circular
  float plungeFraction = 0.05f;   // share of gas spawned with too little angular momentum
};

enum class WaveSource : int {
  PlaneSlice = 0,   // 2D: a line of photons in the equatorial plane
  PlaneSheet = 1,   // 3D: a square sheet of photons
  FlashRing = 2,    // 2D: a circular flash from a point source
  FlashSphere = 3,  // 3D: a spherical flash from a point source
};

struct WaveParams {
  WaveSource source = WaveSource::PlaneSlice;
  int photonsPerFront = 2048;
  float startDistance = 40.0f;   // plane waves start this far upstream
  float halfWidth = 18.0f;       // half-size of the plane wave in impact parameter
  float sourceRadius = 12.0f;    // flash: distance of the source from the hole
  float incidence = 0.0f;        // direction of travel in the disk plane, radians
};

// A star (or gas cloud) on an orbit that brings it close to the hole.
// "Frozen-in" model of a tidal disruption: self-gravity holds the star together
// until its center reaches the disruption radius, so until then it moves as a
// rigid ball along the center-of-mass geodesic; after release every particle is
// a free test particle and tides spread the debris (ΔE ≈ M R / r_t²).
struct StarParams {
  float starRadius = 1.2f;       // M. A real Sun around Sgr A* would be ~0.1 M; bigger reads better on screen.
  float pericenter = 7.0f;       // closest approach of the star's center, M
  float energy = 1.0f;           // specific orbital energy E (1 = parabolic, < 1 bound)
  float startDistance = 60.0f;
  float inclination = 0.0f;      // orbit tilt against the equatorial plane, radians
  float disruptionRadius = 9.0f; // where self-gravity loses to tides (released at pericenter at the latest)

  // Center-of-mass state, maintained by the host while the star is intact.
  Vec3 center{0.0f, 0.0f, 0.0f};
  Vec3 velocity{0.0f, 0.0f, 0.0f};
};

// Initial center-of-mass state from the conserved E and L of a Schwarzschild
// geodesic: at pericenter ṙ = 0, so L² = r_p² (E² / (1 - 2M/r_p) - 1).
BH_HD inline void starOrbitStart(const StarParams& s, Vec3& center, Vec3& velocity) {
  const float rp = fmaxf(s.pericenter, 2.2f);
  const float l2 = fmaxf(rp * rp * (s.energy * s.energy / gr::lapse2(rp) - 1.0f), 0.0f);
  const float r0 = s.startDistance;
  const float rdot2 = s.energy * s.energy - gr::lapse2(r0) * (1.0f + l2 / (r0 * r0));
  const float rdot = -sqrtf(fmaxf(rdot2, 0.0f));  // falling in
  const float vt = sqrtf(l2) / r0;

  // Orbit plane tilted about the x axis; the star starts on the -x side.
  const float ci = cosf(s.inclination), si = sinf(s.inclination);
  const Vec3 radial{-1.0f, 0.0f, 0.0f};
  const Vec3 tangent{0.0f, -si, ci};  // L along +y when untilted
  center = radial * r0;
  velocity = radial * rdot + tangent * vt;
}

struct ScenarioParams {
  ScenarioType type = ScenarioType::AccretionDisk;
  bool respawnDead = false;  // refill dead slots every step (continuous inflow)
  uint32_t runSeed = 0;      // fixed for a whole run (coherent patterns across refills)
  float spin = 0.0f;         // a/M: != 0 spawns Kerr states (Kerr-Schild momentum + energy)
  DiskParams disk;
  WaveParams wave;
  StarParams star;
};

namespace detail {

// Brightness pattern of the gas: a few logarithmic spiral modes. Each particle
// keeps its brightness, so differential rotation keeps winding the pattern up
// into ever tighter filaments, like the turbulent structure of real disks.
BH_HD inline float diskClumps(float r, float phi, float time, uint32_t seed) {
  const float lr = logf(r);
  float f = 0.0f;
  const float modes[3] = {3.0f, 7.0f, 13.0f};
  const float pitch[3] = {4.0f, 9.0f, 17.0f};
  for (int k = 0; k < 3; ++k) {
    Rng rng(static_cast<uint32_t>(k), seed);
    const float phase = rng.uniform(0.0f, 6.28318530718f) + 0.002f * time;
    f += sinf(modes[k] * phi + pitch[k] * lr + phase) / static_cast<float>(k + 1);
  }
  const float n = 0.5f + 0.35f * f;  // ~[0, 1]
  return n * n;
}

BH_HD inline void spawnDisk(const DiskParams& d, uint32_t index, uint32_t seed, uint32_t runSeed, float spin,
                            float time, bool initial, Particle& p) {
  Rng rng(index, seed);
  // Initial disk: log-uniform radii (surface density ∝ 1/r², dense bright inner disk).
  // Refill: new gas arrives at the outer edge.
  const float u = rng.uniform();
  const float r = initial ? d.innerRadius * powf(d.outerRadius / d.innerRadius, u)
                          : d.outerRadius * (0.85f + 0.15f * u);
  const float phi = rng.uniform(0.0f, 6.28318530718f);
  const float c = cosf(phi), s = sinf(phi);

  const Vec3 radial{c, 0.0f, s};
  const Vec3 tangent{s, 0.0f, -c};  // orbit with angular momentum along +y
  const Vec3 up{0.0f, 1.0f, 0.0f};

  const float vc = gr::circularSpeed(fmaxf(r, 3.2f));
  float speed = vc * (1.0f + d.eccentricity * rng.gaussian());
  if (rng.uniform() < d.plungeFraction) speed = vc * rng.uniform(0.55f, 0.9f);

  // One random draw per statement: the evaluation order of operands inside a
  // single expression is unspecified, and MSVC and nvcc do pick different orders.
  const float height = rng.gaussian();
  const float radialKick = rng.gaussian();
  const float verticalKick = rng.gaussian();
  p.x = radial * r + up * (r * d.thickness * height);
  p.v = tangent * speed + radial * (vc * d.eccentricity * 0.5f * radialKick) + up * (vc * d.thickness * verticalKick);
  if (spin != 0.0f) {
    // Kerr: exact prograde circular orbit at Boyer-Lindquist radius rr, then the
    // same relative perturbations of the 4-velocity; the energy comes from the
    // mass shell, so the perturbed state is still a valid geodesic.
    float rr = fmaxf(r, 1.2f), ut = 1.0f, omega = 0.0f;
    for (int k = 0; k < 64 && !kerr::circularOrbit(rr, spin, ut, omega); ++k) rr += 0.1f;  // outside the photon orbit
    const Vec3 X{rr * c + spin * s, rr * s - spin * c, rr * d.thickness * height};
    const Vec3 tangentKS{-X.y, X.x, 0.0f};  // ∂X/∂φ
    const Vec3 radialKS{c, s, 0.0f};        // ∂X/∂r
    const float vOrbit = omega * ut * length(tangentKS);
    const Vec3 velocity = tangentKS * (omega * ut * (speed / vc)) +
                          radialKS * (vOrbit * d.eccentricity * 0.5f * radialKick) +
                          Vec3{0.0f, 0.0f, vOrbit * d.thickness * verticalKick};
    const Vec3 P = kerr::covariantMomentum(X, velocity, ut, spin);
    p.x = kerr::fromKS(X);
    p.v = kerr::fromKS(P);
    p.energy = kerr::energyFor(X, P, spin, 1.0f);
  }
  p.age = 0.0f;
  p.flags = kAlive;
  // Clumpy brightness: spiral pattern times per-particle grain.
  p.tag = (0.15f + 2.2f * diskClumps(r, phi, time, runSeed)) * (0.5f + rng.uniform());
  p.birth = time;
}

BH_HD inline void spawnWave(const WaveParams& w, uint32_t index, float spin, float time, Particle& p) {
  const uint32_t n = w.photonsPerFront > 0 ? static_cast<uint32_t>(w.photonsPerFront) : 1u;
  const uint32_t i = index % n;
  const Vec3 dir{cosf(w.incidence), 0.0f, sinf(w.incidence)};
  const Vec3 up{0.0f, 1.0f, 0.0f};
  const Vec3 side = cross(up, dir);

  Vec3 x, v;
  switch (w.source) {
    case WaveSource::PlaneSlice: {
      const float b = -w.halfWidth + 2.0f * w.halfWidth * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
      x = dir * (-w.startDistance) + side * b;
      v = dir;
      break;
    }
    case WaveSource::PlaneSheet: {
      const uint32_t side_n = static_cast<uint32_t>(sqrtf(static_cast<float>(n)));
      const uint32_t m = side_n > 0 ? side_n : 1u;
      const float a = -w.halfWidth + 2.0f * w.halfWidth * (static_cast<float>(i % m) + 0.5f) / static_cast<float>(m);
      const float b = -w.halfWidth + 2.0f * w.halfWidth * (static_cast<float>((i / m) % m) + 0.5f) / static_cast<float>(m);
      x = dir * (-w.startDistance) + side * a + up * b;
      v = dir;
      break;
    }
    case WaveSource::FlashRing: {
      const float phi = 6.28318530718f * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
      v = dir * cosf(phi) + side * sinf(phi);
      x = dir * (-w.sourceRadius) + v * 0.05f;
      break;
    }
    case WaveSource::FlashSphere:
    default: {
      // Fibonacci sphere: near-uniform directions.
      const float zc = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
      const float rho = sqrtf(fmaxf(0.0f, 1.0f - zc * zc));
      const float phi = 2.39996322973f * static_cast<float>(i);
      v = dir * (rho * cosf(phi)) + side * (rho * sinf(phi)) + up * zc;
      x = dir * (-w.sourceRadius) + v * 0.05f;
      break;
    }
  }
  p.x = x;
  p.v = v;
  if (spin != 0.0f) {  // null Kerr-Schild momentum along v with E = 1
    p.v = kerr::fromKS(kerr::nullMomentum(kerr::toKS(x), kerr::toKS(normalize(v)), spin));
    p.energy = 1.0f;
  }
  p.age = 0.0f;
  p.flags = kAlive | kPhoton;
  p.tag = 0.0f;
  p.birth = time;
}

BH_HD inline void spawnStar(const StarParams& s, uint32_t index, uint32_t runSeed, float spin, float time,
                            Particle& p) {
  // Centrally concentrated ball. The offset depends only on (index, runSeed),
  // so re-spawning every frame while the star is intact keeps it rigid.
  // tag = depth inside the star (0 core, 1 surface).
  Rng rng(index, runSeed);
  const float z = rng.uniform(-1.0f, 1.0f);
  const float phi = rng.uniform(0.0f, 6.28318530718f);
  const float rho = sqrtf(fmaxf(0.0f, 1.0f - z * z));
  const float depth = powf(rng.uniform(), 0.5f);
  const Vec3 offset = Vec3{rho * cosf(phi), z, rho * sinf(phi)} * (depth * s.starRadius);

  p.x = s.center + offset;
  p.v = s.velocity;  // Kerr: the center's covariant momentum (the star moves as one body)
  if (spin != 0.0f) p.energy = kerr::energyFor(kerr::toKS(p.x), kerr::toKS(p.v), spin, 1.0f);
  p.age = 0.0f;
  p.flags = kAlive;
  p.tag = depth;
  p.birth = time;
}

}  // namespace detail

// Fills `p` with a fresh particle for slot `index`. `initial` distinguishes the
// first population from refills of dead slots.
BH_HD inline void spawnParticle(const ScenarioParams& sc, uint32_t index, uint32_t seed, float time, bool initial,
                                Particle& p) {
  switch (sc.type) {
    case ScenarioType::AccretionDisk:
      detail::spawnDisk(sc.disk, index, seed, sc.runSeed, sc.spin, time, initial, p);
      break;
    case ScenarioType::PhotonWaves: detail::spawnWave(sc.wave, index, sc.spin, time, p); break;
    case ScenarioType::StarDisruption: detail::spawnStar(sc.star, index, sc.runSeed, sc.spin, time, p); break;
  }
}

}  // namespace bh
