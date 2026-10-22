#pragma once

#include "sim/scenarios/Scenario.hpp"

namespace bh {

// Light as a sequence of wave fronts: each front is a set of photons emitted at
// the same moment, so at any instant the photons of a front trace the shape of
// the wavefront. Watch fronts wrap around the photon sphere, slow down near the
// hole (Shapiro delay) and leave a shadow behind it (photons with b < 3√3 M).
class PhotonWaves final : public Scenario {
 public:
  const char* name() const override { return "Light waves"; }
  const char* subtitle() const override { return "Wave fronts of light bending around the shadow"; }

  size_t particleCount() const override;
  ScenarioParams params() const override;
  void configureStep(StepParams& sp) const override;
  void reset(SimBackend& backend, uint32_t seed, float time) override;
  void update(SimBackend& backend, float time) override;

  std::vector<ParamDesc> parameters() override;
  std::vector<QuantityOption> quantities() const override;
  ScenarioDefaults defaults() const override;

  float lifetime() const override { return static_cast<float>(fronts_) * period_; }

 private:
  void emitFront(SimBackend& backend, float time);

  WaveParams wave_;
  int source_ = static_cast<int>(WaveSource::PlaneSlice);
  int fronts_ = 24;
  float period_ = 5.0f;  // M/c between fronts
  float incidenceDeg_ = 0.0f;

  // Emission schedule (fronts live in a ring buffer of slots).
  int nextSlot_ = 0;
  float nextEmission_ = 0.0f;
};

}  // namespace bh
