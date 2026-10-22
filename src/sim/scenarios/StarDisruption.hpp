#pragma once

#include "sim/scenarios/Scenario.hpp"

namespace bh {

// Tidal disruption event: a star on a close orbit is stretched into a stream
// ("spaghettification"). Roughly half the debris is bound and comes back, the
// rest escapes. Particles are colored by the layer of the star they came from.
class StarDisruption final : public Scenario {
 public:
  const char* name() const override { return "Star"; }
  const char* subtitle() const override { return "A star torn apart by tides on a close pass"; }

  size_t particleCount() const override { return static_cast<size_t>(count_); }
  ScenarioParams params() const override;
  void configureStep(StepParams& sp) const override;
  void reset(SimBackend& backend, uint32_t seed, float time) override;
  void update(SimBackend& backend, float time, const StepParams& sp) override;

  std::vector<ParamDesc> parameters() override;
  std::vector<QuantityOption> quantities() const override;
  ScenarioDefaults defaults() const override;

 private:
  int count_ = 250000;
  StarParams star_;
  float inclinationDeg_ = 0.0f;

  // Center of mass while the star is intact (integrated on the host).
  Particle center_{};
  bool released_ = false;
  uint32_t runSeed_ = 0;
};

}  // namespace bh
