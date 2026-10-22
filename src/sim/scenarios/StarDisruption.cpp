#include "sim/scenarios/StarDisruption.hpp"

namespace bh {

ScenarioParams StarDisruption::params() const {
  ScenarioParams p;
  p.type = ScenarioType::StarDisruption;
  p.respawnDead = false;  // a one-off event: press Restart to replay
  p.star = star_;
  p.star.inclination = inclinationDeg_ * 0.01745329252f;
  p.star.center = center_.x;
  p.star.velocity = center_.v;
  return p;
}

void StarDisruption::configureStep(StepParams& sp) const { sp.outerRadius = 6.0f * star_.startDistance; }

void StarDisruption::reset(SimBackend& backend, uint32_t seed, float time) {
  released_ = false;
  StarParams s = star_;
  s.inclination = inclinationDeg_ * 0.01745329252f;
  center_ = Particle{};
  starOrbitStart(s, center_.x, center_.v);
  // Kerr: the Schwarzschild orbit's dx/dτ is used as the Kerr-Schild spatial
  // 4-velocity at the (distant) start; store the covariant momentum and energy.
  if (spin_ != 0.0f) {
    const Vec3 X = kerr::toKS(center_.x);
    const Vec3 P = kerr::momentumFromVelocity(X, kerr::toKS(center_.v), spin_);
    center_.v = kerr::fromKS(P);
    center_.energy = kerr::energyFor(X, P, spin_, 1.0f);
  }
  center_.flags = kAlive;
  backend.spawn(kernelParams(), 0, backend.size(), seed, time, /*initial=*/true);
}

void StarDisruption::update(SimBackend& backend, float time, const StepParams& sp) {
  if (released_) return;
  // Re-place the intact star on its center-of-mass orbit, then decide whether
  // tides win from the next step on.
  backend.spawn(kernelParams(), 0, backend.size(), runSeed_, time, true);

  const Vec3 x0 = center_.x;
  StepParams cm = sp;
  cm.drag = 0.0f;
  if (!advanceParticle(center_, cm)) {
    released_ = true;  // the whole star plunged
    return;
  }
  const float r = length(center_.x);
  const bool pastPericenter = dot(center_.x, center_.v) > 0.0f && length(x0) < star_.startDistance * 0.5f;
  if (r <= star_.disruptionRadius || pastPericenter) released_ = true;
}

std::vector<ParamDesc> StarDisruption::parameters() {
  return {
      ParamDesc::integer("Particles", &count_, 10000, 20000000, true, nullptr, true),
      ParamDesc::slider("Pericenter", &star_.pericenter, 3.0f, 40.0f, "%.1f M", true,
                        "Closest approach of the star's center. Parabolic orbits with pericenter below 4 M are "
                        "captured whole."),
      ParamDesc::slider("Disruption radius", &star_.disruptionRadius, 3.0f, 60.0f, "%.1f M", true,
                        "Where tides overcome the star's own gravity (the tidal radius). The star moves as one "
                        "body until here, then every particle flies freely. Debris energy spread ~ M R / r_t^2: "
                        "smaller values tear it apart harder."),
      ParamDesc::slider("Star radius", &star_.starRadius, 0.1f, 8.0f, "%.2f M", true,
                        "A Sun-like star near Sagittarius A* would be about 0.1 M across. Big values model a gas "
                        "cloud.",
                        true),
      ParamDesc::slider("Orbit energy", &star_.energy, 0.90f, 1.05f, "%.3f", true,
                        "Specific energy E: 1 = parabolic (typical for disruptions), < 1 = bound orbit that keeps "
                        "returning and precessing."),
      ParamDesc::slider("Inclination", &inclinationDeg_, 0.0f, 90.0f, "%.0f deg", true),
      ParamDesc::slider("Start distance", &star_.startDistance, 20.0f, 150.0f, "%.0f M", true),
  };
}

std::vector<QuantityOption> StarDisruption::quantities() const {
  return {
      {Quantity::Tag, "Layer in star", 0.0f, 1.0f},
      {Quantity::Speed, "Speed |v|/c", 0.0f, 0.6f},
      {Quantity::Redshift, "Redshift g", 0.4f, 1.6f},
      {Quantity::TimeDilation, "Time dilation", 0.3f, 1.0f},
      {Quantity::Radius, "Radius", 2.0f, 60.0f},
  };
}

ScenarioDefaults StarDisruption::defaults() const {
  ScenarioDefaults d;
  d.appearance.style = ColorStyle::Colormap;
  d.appearance.quantity = Quantity::Tag;
  d.appearance.colormap = Colormap::Plasma;
  d.appearance.rangeMin = 0.0f;
  d.appearance.rangeMax = 1.0f;
  d.appearance.pointSize = 0.12f;
  d.appearance.brightness = 0.25f;
  d.appearance.dopplerBeaming = false;
  d.appearance.lensSource = Appearance::Particles;
  d.appearance.volumeExtent = 120.0f;
  d.view = ViewPreset{0.3f, 1.0f, 120.0f};
  d.timeScale = 30.0f;
  d.timeMode = TimeMode::Coordinate;
  return d;
}

}  // namespace bh
