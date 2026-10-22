#pragma once

#include "core/Random.hpp"
#include "sim/Particle.hpp"

// Initial conditions for every scenario, written as plain host/device functions
// so they can run inside a CUDA kernel later (no virtual calls, POD params).
namespace bh {

enum class ScenarioType : int { AccretionDisk = 0, PhotonWaves = 1 };

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

struct ScenarioParams {
  ScenarioType type = ScenarioType::AccretionDisk;
  bool respawnDead = false;  // refill dead slots every step (continuous inflow)
  uint32_t runSeed = 0;      // fixed for a whole run (coherent patterns across refills)
  DiskParams disk;
  WaveParams wave;
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

BH_HD inline void spawnDisk(const DiskParams& d, uint32_t index, uint32_t seed, uint32_t runSeed, float time,
                            bool initial, Particle& p) {
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

  p.x = radial * r + up * (r * d.thickness * rng.gaussian());
  p.v = tangent * speed + radial * (vc * d.eccentricity * 0.5f * rng.gaussian()) +
        up * (vc * d.thickness * rng.gaussian());
  p.age = 0.0f;
  p.flags = kAlive;
  // Clumpy brightness: spiral pattern times per-particle grain.
  p.tag = (0.15f + 2.2f * diskClumps(r, phi, time, runSeed)) * (0.5f + rng.uniform());
  p.birth = time;
}

BH_HD inline void spawnWave(const WaveParams& w, uint32_t index, float time, Particle& p) {
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
  p.age = 0.0f;
  p.flags = kAlive | kPhoton;
  p.tag = 0.0f;
  p.birth = time;
}

}  // namespace detail

// Fills `p` with a fresh particle for slot `index`. `initial` distinguishes the
// first population from refills of dead slots.
BH_HD inline void spawnParticle(const ScenarioParams& sc, uint32_t index, uint32_t seed, float time, bool initial,
                                Particle& p) {
  switch (sc.type) {
    case ScenarioType::AccretionDisk: detail::spawnDisk(sc.disk, index, seed, sc.runSeed, time, initial, p); break;
    case ScenarioType::PhotonWaves: detail::spawnWave(sc.wave, index, time, p); break;
  }
}

}  // namespace bh
