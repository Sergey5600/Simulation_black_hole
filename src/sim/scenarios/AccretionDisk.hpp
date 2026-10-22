#pragma once

#include "sim/scenarios/Scenario.hpp"

namespace bh {

// A thin disk of gas on (nearly) circular geodesics. Viscous drag makes it
// spiral in through the ISCO and plunge; new gas is fed in at the outer edge.
class AccretionDisk final : public Scenario {
 public:
  const char* name() const override { return "Accretion disk"; }
  const char* subtitle() const override { return "Gas on Schwarzschild geodesics, colored by what it emits"; }

  size_t particleCount() const override { return static_cast<size_t>(count_); }
  ScenarioParams params() const override;
  void configureStep(StepParams& sp) const override;
  void reset(SimBackend& backend, uint32_t seed, float time) override;
  bool tagIsBrightness() const override { return true; }

  std::vector<ParamDesc> parameters() override;
  std::vector<QuantityOption> quantities() const override;
  ScenarioDefaults defaults() const override;

 private:
  int count_ = 300000;
  DiskParams disk_;
  float drag_ = 0.0015f;
};

}  // namespace bh
