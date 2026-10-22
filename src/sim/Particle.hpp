#pragma once

#include <cstdint>

#include "core/Schwarzschild.hpp"

namespace bh {

enum ParticleFlags : uint32_t {
  kAlive = 1u << 0,
  kPhoton = 1u << 1,
};

// Simulation state of one test particle (gas element or photon).
struct Particle {
  Vec3 x;          // pseudo-Cartesian position (Schwarzschild r · direction), units of M
  float age;       // time since spawn (same clock the simulation advances)
  Vec3 v;          // dx/dλ
  uint32_t flags;  // ParticleFlags
  float tag;       // scenario-defined: brightness variation for gas, unused for photons
  float birth;     // simulation time at spawn
};

BH_HD BH_INLINE bool isAlive(const Particle& p) { return (p.flags & kAlive) != 0u; }
BH_HD BH_INLINE gr::Kind kindOf(const Particle& p) {
  return (p.flags & kPhoton) ? gr::Kind::Photon : gr::Kind::Massive;
}

// What the renderer receives per point (32 bytes, tightly packed for the VBO).
// The meaning of `scalar`, `dir` and `value` depends on the particle kind:
//   gas:    scalar = brightness variation, dir = local 3-velocity β, value = dτ/dt
//   photon: scalar = age,                  dir = propagation direction, value = impact parameter b
// value < 0 marks a dead slot that the vertex shader discards.
struct PointVertex {
  float pos[3];
  float scalar;
  float dir[3];
  float value;
};
static_assert(sizeof(PointVertex) == 32, "PointVertex must stay tightly packed");

BH_HD BH_INLINE PointVertex makeVertex(const Particle& p) {
  PointVertex out{};
  out.pos[0] = p.x.x;
  out.pos[1] = p.x.y;
  out.pos[2] = p.x.z;
  if (!isAlive(p)) {
    out.value = -1.0f;
    return out;
  }
  Vec3 dir;
  if (p.flags & kPhoton) {
    const float e = fmaxf(gr::energy(p.x, p.v, 0.0f), 1e-6f);
    dir = normalize(p.v);
    out.scalar = p.age;
    out.value = sqrtf(gr::angularMomentum2(p.x, p.v)) / e;  // b = L / E
  } else {
    const gr::LocalKinematics k = gr::localKinematics(p.x, p.v);
    dir = k.beta;
    out.scalar = p.tag;
    out.value = k.timeDilation;
  }
  out.dir[0] = dir.x;
  out.dir[1] = dir.y;
  out.dir[2] = dir.z;
  return out;
}

}  // namespace bh
