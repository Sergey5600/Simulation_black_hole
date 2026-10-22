#pragma once

#include <cstdint>
#include <vector>

#include "backend/SimBackend.hpp"
#include "sim/Appearance.hpp"
#include "sim/Parameters.hpp"

namespace bh {

// One way of filling space with particles: an accretion disk, light waves, ...
// A scenario owns its parameters, decides when particles are (re)spawned and
// suggests how it should look. The heavy per-particle work happens in the backend.
struct QuantityOption {
  Quantity quantity;
  const char* label;
  float defaultMin;
  float defaultMax;
};

struct ScenarioDefaults {
  Appearance appearance;
  ViewPreset view;
  float timeScale = 30.0f;  // simulated M/c per real second
  TimeMode timeMode = TimeMode::Coordinate;
};

class Scenario {
 public:
  virtual ~Scenario() = default;

  virtual const char* name() const = 0;
  virtual const char* subtitle() const = 0;

  virtual size_t particleCount() const = 0;
  // Kernel parameters; the Simulation fills in ScenarioParams::runSeed.
  virtual ScenarioParams params() const = 0;

  // Lets the scenario adjust integration (drag, domain size...) every frame.
  virtual void configureStep(StepParams& /*sp*/) const {}

  // Fills the (already resized) backend with the initial population.
  // `seed` is constant for the run: use it as both spawn seed and runSeed.
  virtual void reset(SimBackend& backend, uint32_t seed, float time) = 0;

  // Called once per simulation step before integration (e.g. to emit wave
  // fronts). `sp` is the step about to be taken.
  virtual void update(SimBackend& /*backend*/, float /*time*/, const StepParams& /*sp*/) {}

  // Whether Particle::tag is a brightness factor (true) or data for Quantity::Tag.
  virtual bool tagIsBrightness() const { return false; }

  // Age at which particles are recycled (0 = they live until they die).
  virtual float lifetime() const { return 0.0f; }

  virtual std::vector<ParamDesc> parameters() = 0;
  virtual std::vector<QuantityOption> quantities() const = 0;
  virtual ScenarioDefaults defaults() const = 0;
};

}  // namespace bh
