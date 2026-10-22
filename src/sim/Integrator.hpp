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
  float spin = 0.0f;             // a/M; != 0 switches to the Kerr integrator
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

// Kerr: RK4 on Hamilton's equations in Cartesian Kerr-Schild coordinates
// (core/Kerr.hpp), stepping the affine parameter λ (= proper time for matter).
// In observer-time mode each step covers h_λ · dt_BL/dλ of distant-observer
// time, which diverges at the horizon: infalling matter freezes there.
BH_HD inline bool advanceParticleKerr(Particle& p, const StepParams& sp) {
  const float a = sp.spin;
  const float kappa = gr::kappaOf(kindOf(p));
  const bool coordinate = sp.timeMode == TimeMode::Coordinate;
  const float rh = kerr::horizon(a);
  Vec3 X = kerr::toKS(p.x), P = kerr::toKS(p.v);
  float E = p.energy;

  float remaining = sp.dt;
  for (int i = 0; i < sp.maxSubsteps && remaining > 0.0f; ++i) {
    const float r = kerr::radius(X, a);
    const bool atHorizon = coordinate ? r < rh * (1.0f + sp.horizonLapse) : r <= rh;
    if (atHorizon || r > sp.outerRadius || !(r == r) || !(E == E)) {
      p.flags &= ~kAlive;
      p.x = kerr::fromKS(X);
      return false;
    }

    Vec3 dx1, dp1;
    kerr::derivatives(X, P, E, a, dx1, dp1);
    const float rate = coordinate ? fmaxf(kerr::observerTimeRate(X, P, dx1, E, a), 1e-4f) : 1.0f;
    const float stepLambda = sp.accuracy * (kappa > 0.0f ? r * sqrtf(r) : r);
    const float h = fminf(stepLambda, remaining / rate);

    Vec3 dx2, dp2, dx3, dp3, dx4, dp4;
    kerr::derivatives(X + dx1 * (0.5f * h), P + dp1 * (0.5f * h), E, a, dx2, dp2);
    kerr::derivatives(X + dx2 * (0.5f * h), P + dp2 * (0.5f * h), E, a, dx3, dp3);
    kerr::derivatives(X + dx3 * h, P + dp3 * h, E, a, dx4, dp4);
    const float w = h / 6.0f;
    X += (dx1 + dx2 * 2.0f + dx3 * 2.0f + dx4) * w;
    P += (dp1 + dp2 * 2.0f + dp3 * 2.0f + dp4) * w;

    if (sp.drag > 0.0f && kappa > 0.0f) {  // viscous drag on the azimuthal momentum
      const Vec3 n = normalize(X);
      const Vec3 pPerp = P - n * dot(n, P);
      P -= pPerp * fminf(sp.drag * h, 0.5f);
      E = kerr::energyFor(X, P, a, kappa);
    }

    remaining -= h * rate;
    p.age += h * rate;
  }
  p.x = kerr::fromKS(X);
  p.v = kerr::fromKS(P);
  p.energy = E;
  return true;
}

// Advances one particle by sp.dt with adaptive RK4 substeps. Marks it dead when
// it reaches the horizon or leaves the domain. Returns whether it is still alive.
BH_HD inline bool advanceParticle(Particle& p, const StepParams& sp) {
  if (!isAlive(p)) return false;
  if (sp.spin != 0.0f) return advanceParticleKerr(p, sp);
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
