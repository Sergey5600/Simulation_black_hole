#pragma once

#include <cstdint>

#include "core/Kerr.hpp"
#include "core/Schwarzschild.hpp"

namespace bh {

enum ParticleFlags : uint32_t {
  kAlive = 1u << 0,
  kPhoton = 1u << 1,
};

// Simulation state of one test particle (gas element or photon).
// Non-spinning hole (a = 0): x is the pseudo-Cartesian position (Schwarzschild
// r · direction) and v = dx/dλ. Spinning hole: x is the Cartesian Kerr-Schild
// position (world axes), v the covariant spatial momentum p_i and energy = -p_t.
struct Particle {
  Vec3 x;          // position, units of M
  float age;       // time since spawn (same clock the simulation advances)
  Vec3 v;          // dx/dλ (a = 0) or p_i (a != 0)
  uint32_t flags;  // ParticleFlags
  float tag;       // scenario-defined: brightness variation for gas, unused for photons
  float birth;     // simulation time at spawn
  float energy;    // a != 0 only: conserved E = -p_t
  float pad;
};

BH_HD BH_INLINE bool isAlive(const Particle& p) { return (p.flags & kAlive) != 0u; }
BH_HD BH_INLINE gr::Kind kindOf(const Particle& p) {
  return (p.flags & kPhoton) ? gr::Kind::Photon : gr::Kind::Massive;
}

// What the renderer receives per point (32 bytes, tightly packed for the VBO).
// The meaning of `scalar`, `dir` and `value` depends on the particle kind:
//   gas:    scalar = brightness variation, dir = local 3-velocity β, value = dτ/dt
//           (spinning hole: dir = coordinate velocity dx/dt, a close stand-in for β)
//   photon: scalar = age,                  dir = propagation direction, value = impact parameter b
// value < 0 marks a dead slot that the vertex shader discards.
struct PointVertex {
  float pos[3];
  float scalar;
  float dir[3];
  float value;
};
static_assert(sizeof(PointVertex) == 32, "PointVertex must stay tightly packed");

BH_HD inline PointVertex makeVertexKerr(const Particle& p, float a);

BH_HD BH_INLINE PointVertex makeVertex(const Particle& p, float spin = 0.0f) {
  if (spin != 0.0f) return makeVertexKerr(p, spin);
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

BH_HD inline PointVertex makeVertexKerr(const Particle& p, float a) {
  PointVertex out{};
  out.pos[0] = p.x.x;
  out.pos[1] = p.x.y;
  out.pos[2] = p.x.z;
  if (!isAlive(p)) {
    out.value = -1.0f;
    return out;
  }
  const Vec3 X = kerr::toKS(p.x), P = kerr::toKS(p.v);
  Vec3 dX, dP;
  kerr::derivatives(X, P, p.energy, a, dX, dP);
  Vec3 dir;
  if (p.flags & kPhoton) {
    dir = normalize(kerr::fromKS(dX));
    out.scalar = p.age;
    out.value = length(cross(X, P)) / fmaxf(p.energy, 1e-6f);  // b = L / E
  } else {
    const float rate = fmaxf(kerr::observerTimeRate(X, P, dX, p.energy, a), 1e-6f);  // dt/dτ
    dir = kerr::fromKS(dX) / rate;  // coordinate velocity dx/dt
    out.scalar = p.tag;
    out.value = 1.0f / rate;        // dτ/dt
  }
  out.dir[0] = dir.x;
  out.dir[1] = dir.y;
  out.dir[2] = dir.z;
  return out;
}

}  // namespace bh
