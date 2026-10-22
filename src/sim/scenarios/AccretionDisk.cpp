#include "sim/scenarios/AccretionDisk.hpp"

namespace bh {

ScenarioParams AccretionDisk::params() const {
  ScenarioParams p;
  p.type = ScenarioType::AccretionDisk;
  p.respawnDead = true;
  p.disk = disk_;
  return p;
}

void AccretionDisk::configureStep(StepParams& sp) const {
  sp.drag = drag_;
  sp.outerRadius = disk_.outerRadius * 3.0f;
}

void AccretionDisk::reset(SimBackend& backend, uint32_t seed, float time) {
  ScenarioParams p = params();
  p.runSeed = seed;
  backend.spawn(p, 0, backend.size(), seed, time, /*initial=*/true);
}

std::vector<ParamDesc> AccretionDisk::parameters() {
  return {
      ParamDesc::integer("Particles", &count_, 10000, 20000000, true,
                         "Number of gas elements. CPU: ~0.5M at 60 FPS. CUDA: tens of millions.", true),
      ParamDesc::slider("Inner radius", &disk_.innerRadius, 3.5f, 20.0f, "%.1f M", true,
                        "Where the initial disk starts. ISCO = 6 M: inside it no stable circular orbit exists."),
      ParamDesc::slider("Outer radius", &disk_.outerRadius, 10.0f, 150.0f, "%.0f M", true),
      ParamDesc::slider("Thickness", &disk_.thickness, 0.0f, 0.25f, "%.3f h/r", true),
      ParamDesc::slider("Eccentricity", &disk_.eccentricity, 0.0f, 0.3f, "%.3f", true,
                        "Random spread of orbital speeds around the circular value."),
      ParamDesc::slider("Plunging gas", &disk_.plungeFraction, 0.0f, 0.5f, "%.2f", true,
                        "Fraction of gas spawned with too little angular momentum to stay in orbit."),
      ParamDesc::slider("Viscous drag", &drag_, 0.0f, 0.02f, "%.4f", false,
                        "Phenomenological viscosity (alpha-disk stand-in). 0 = pure geodesics: gas never "
                        "accretes, only orbits.",
                        false),
  };
}

std::vector<QuantityOption> AccretionDisk::quantities() const {
  return {
      {Quantity::Temperature, "Temperature", 0.0f, 1.0f},
      {Quantity::Redshift, "Redshift g", 0.4f, 1.6f},
      {Quantity::Speed, "Speed |v|/c", 0.0f, 0.7f},
      {Quantity::TimeDilation, "Time dilation", 0.3f, 1.0f},
      {Quantity::Radius, "Radius", 2.0f, 40.0f},
  };
}

ScenarioDefaults AccretionDisk::defaults() const {
  ScenarioDefaults d;
  d.appearance.style = ColorStyle::Blackbody;
  d.appearance.quantity = Quantity::Temperature;
  d.appearance.colormap = Colormap::Inferno;
  d.appearance.rangeMin = 0.0f;
  d.appearance.rangeMax = 1.0f;
  d.appearance.pointSize = 0.25f;
  d.appearance.brightness = 1.0f;
  d.appearance.peakTemperature = 5800.0f;
  d.appearance.lensSource = Appearance::ThinDisk;
  d.appearance.volumeExtent = 90.0f;
  d.view = ViewPreset{0.0f, 0.18f, 70.0f};
  d.timeScale = 30.0f;
  d.timeMode = TimeMode::Coordinate;
  return d;
}

}  // namespace bh
