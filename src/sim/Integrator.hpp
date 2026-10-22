#pragma once

#include "sim/Particle.hpp"

namespace bh {

// Which clock the whole simulation advances.
enum class TimeMode : int {
  // Coordinate time t of a distant observer (what a telescope would see):
  // infalling matter slows down, reddens and "freezes" at the horizon.
  Coordinate = 0,
  // Each particle's own proper time τ (affine parameter for photons):
  // matter crosses the horizon in finite time.
  Proper = 1,
};

struct StepParams {
  float dt = 0.0f;               // how much the chosen clock advances this frame
  TimeMode timeMode = TimeMode::Coordinate;
  float accuracy = 0.02f;        // step = accuracy × local dynamical time
  int maxSubsteps = 256;         // safety cap per particle per frame
  float drag = 0.0f;             // phenomenological viscosity (massive only), 1/time
  float outerRadius = 500.0f;    // particles beyond this are removed
  float horizonLapse = 0.02f;    // coordinate mode: remove once 1 - 2M/r drops below this
};

namespace detail {

struct State {
  Vec3 x, v;
};

// Right-hand side of the ODE in the chosen clock: d/ds (x, v) = (v, a) · dλ/ds.
// In proper mode dλ/ds = 1; in coordinate mode dλ/dt = (1 - 2M/r) / E.
BH_HD BH_INLINE State derivative(const State& s, float h2, float kappa, float invEnergy, bool coordinate) {
  const Vec3 a = gr::acceleration(s.x, h2, kappa);
  float scale = 1.0f;
  if (coordinate) scale = gr::lapse2(length(s.x)) * invEnergy;
  return {s.v * scale, a * scale};
}

}  // namespace detail

// Advances one particle by sp.dt with adaptive RK4 substeps. Marks it dead when
// it reaches the horizon or leaves the domain. Returns whether it is still alive.
BH_HD inline bool advanceParticle(Particle& p, const StepParams& sp) {
  if (!isAlive(p)) return false;
  const float kappa = gr::kappaOf(kindOf(p));
  const bool coordinate = sp.timeMode == TimeMode::Coordinate;

  float remaining = sp.dt;
  for (int i = 0; i < sp.maxSubsteps && remaining > 0.0f; ++i) {
    const float r = length(p.x);
    const float f = gr::lapse2(r);
    const bool atHorizon = coordinate ? f < sp.horizonLapse : r <= gr::kHorizon;
    if (atHorizon || r > sp.outerRadius || !(r == r)) {
      p.flags &= ~kAlive;
      return false;
    }

    // Conserved along the exact trajectory: evaluate once per substep.
    const float h2 = gr::angularMomentum2(p.x, p.v);
    const float e = fmaxf(gr::energy(p.x, p.v, kappa), 1e-6f);
    const float invE = 1.0f / e;

    // Natural time scale: orbital time ~ r^1.5 for matter, light-crossing ~ r for photons.
    const float stepLambda = sp.accuracy * (kappa > 0.0f ? r * sqrtf(r) : r);
    const float lambdaPerClock = coordinate ? f * invE : 1.0f;
    const float h = fminf(remaining, stepLambda / lambdaPerClock);

    const detail::State s0{p.x, p.v};
    const detail::State k1 = detail::derivative(s0, h2, kappa, invE, coordinate);
    const detail::State k2 = detail::derivative({s0.x + k1.x * (0.5f * h), s0.v + k1.v * (0.5f * h)}, h2, kappa, invE, coordinate);
    const detail::State k3 = detail::derivative({s0.x + k2.x * (0.5f * h), s0.v + k2.v * (0.5f * h)}, h2, kappa, invE, coordinate);
    const detail::State k4 = detail::derivative({s0.x + k3.x * h, s0.v + k3.v * h}, h2, kappa, invE, coordinate);
    const float w = h / 6.0f;
    p.x = s0.x + (k1.x + 2.0f * k2.x + 2.0f * k3.x + k4.x) * w;
    p.v = s0.v + (k1.v + 2.0f * k2.v + 2.0f * k3.v + k4.v) * w;

    // Viscous drag: bleeds angular momentum so gas spirals inward like in a real
    // accretion disk (an alpha-disk stand-in; pure geodesics never accrete).
    if (sp.drag > 0.0f && kappa > 0.0f) {
      const Vec3 n = normalize(p.x);
      const Vec3 vPerp = p.v - n * dot(n, p.v);
      p.v -= vPerp * fminf(sp.drag * h * lambdaPerClock, 0.5f);
    }

    remaining -= h;
    p.age += h;
  }
  return true;
}

}  // namespace bh
