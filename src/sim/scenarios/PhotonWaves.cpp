#include "sim/scenarios/PhotonWaves.hpp"

#include <algorithm>

namespace bh {

namespace {
constexpr const char* kSourceNames[] = {"Plane wave (2D slice)", "Plane wave (3D sheet)", "Flash (2D ring)",
                                        "Flash (3D sphere)"};
}

size_t PhotonWaves::particleCount() const {
  return static_cast<size_t>(fronts_) * static_cast<size_t>(wave_.photonsPerFront);
}

ScenarioParams PhotonWaves::params() const {
  ScenarioParams p;
  p.type = ScenarioType::PhotonWaves;
  p.respawnDead = false;
  p.wave = wave_;
  p.wave.source = static_cast<WaveSource>(source_);
  p.wave.incidence = incidenceDeg_ * 0.01745329252f;
  return p;
}

void PhotonWaves::configureStep(StepParams& sp) const {
  const float reach = std::max(wave_.startDistance, wave_.sourceRadius);
  sp.outerRadius = std::max(3.0f * reach, reach + lifetime() + 10.0f);
}

void PhotonWaves::reset(SimBackend& /*backend*/, uint32_t /*seed*/, float time) {
  // All slots start dead; fronts are emitted by update().
  nextSlot_ = 0;
  nextEmission_ = time;
}

void PhotonWaves::update(SimBackend& backend, float time) {
  // Catch up on every front that should have been emitted by now (bounded so a
  // huge time step cannot stall the frame).
  for (int guard = 0; time >= nextEmission_ && guard < fronts_; ++guard) {
    emitFront(backend, nextEmission_);
    nextEmission_ += period_;
  }
  if (time >= nextEmission_) nextEmission_ = time + period_;
}

void PhotonWaves::emitFront(SimBackend& backend, float time) {
  const size_t perFront = static_cast<size_t>(wave_.photonsPerFront);
  backend.spawn(params(), static_cast<size_t>(nextSlot_) * perFront, perFront, 0u, time, true);
  nextSlot_ = (nextSlot_ + 1) % fronts_;
}

std::vector<ParamDesc> PhotonWaves::parameters() {
  return {
      ParamDesc::choice("Source", &source_, kSourceNames, true),
      ParamDesc::integer("Photons per front", &wave_.photonsPerFront, 64, 65536, true,
                         "For the 3D sheet this is rounded down to a square grid.", true),
      ParamDesc::integer("Fronts", &fronts_, 1, 64, true, "How many wave fronts exist at once."),
      ParamDesc::slider("Period", &period_, 1.0f, 20.0f, "%.1f M/c", false, "Time between two wave fronts."),
      ParamDesc::slider("Incidence", &incidenceDeg_, -180.0f, 180.0f, "%.0f deg", false,
                        "Direction of travel in the disk plane (applies to new fronts)."),
      ParamDesc::slider("Beam half-width", &wave_.halfWidth, 2.0f, 60.0f, "%.1f M", false,
                        "Plane waves: largest impact parameter. The shadow is b < 3*sqrt(3) M = 5.2 M."),
      ParamDesc::slider("Start distance", &wave_.startDistance, 10.0f, 120.0f, "%.0f M", false),
      ParamDesc::slider("Flash radius", &wave_.sourceRadius, 2.5f, 60.0f, "%.1f M", false,
                        "Flash sources: distance of the emitter from the hole. Try 3 M (photon sphere)."),
  };
}

std::vector<QuantityOption> PhotonWaves::quantities() const {
  return {
      {Quantity::Age, "Wave phase", 1.0f, 0.0f},  // reversed: newest fronts brightest
      {Quantity::ImpactParameter, "Impact parameter b", 0.0f, 18.0f},
      {Quantity::Radius, "Radius", 2.0f, 40.0f},
  };
}

ScenarioDefaults PhotonWaves::defaults() const {
  ScenarioDefaults d;
  d.appearance.style = ColorStyle::Colormap;
  d.appearance.quantity = Quantity::Age;
  d.appearance.colormap = Colormap::Nebula;
  d.appearance.rangeMin = 1.0f;
  d.appearance.rangeMax = 0.0f;
  d.appearance.pointSize = 0.16f;
  d.appearance.brightness = 1.2f;
  d.appearance.fadeWithAge = true;
  d.view = ViewPreset{0.0f, 1.3f, 75.0f};
  d.timeScale = 12.0f;
  d.timeMode = TimeMode::Coordinate;
  return d;
}

}  // namespace bh
