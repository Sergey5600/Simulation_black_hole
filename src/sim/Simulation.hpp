#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "backend/SimBackend.hpp"
#include "sim/scenarios/Scenario.hpp"

namespace bh {

struct SimControls {
  bool paused = false;
  float timeScale = 30.0f;  // simulated M/c per real second
  TimeMode timeMode = TimeMode::Coordinate;
  float accuracy = 0.02f;   // RK4 step as a fraction of the local dynamical time
  float spin = 0.0f;        // a/M of the hole (Kerr when != 0); takes effect on reset()
};

// Glue between scenarios and the compute backend. Owns the simulation clock.
class Simulation {
 public:
  explicit Simulation(std::unique_ptr<SimBackend> backend);

  void addScenario(std::unique_ptr<Scenario> scenario);
  size_t scenarioCount() const { return scenarios_.size(); }
  Scenario& scenario(size_t i) { return *scenarios_[i]; }
  Scenario& current() { return *scenarios_[current_]; }
  size_t currentIndex() const { return current_; }

  // Swaps the compute backend (CPU / CUDA) and re-spawns the current scenario.
  void setBackend(std::unique_ptr<SimBackend> backend);

  // Switches scenario and re-spawns everything.
  void select(size_t index);
  // Re-spawns the current scenario from t = 0 (picks up changed counts).
  void reset();

  // Advances by realDt seconds of wall time (scaled by controls().timeScale).
  void update(float realDt);
  // Advances exactly one step of `simDt` regardless of pause (used for warm-up).
  void advance(float simDt);

  SimControls& controls() { return controls_; }
  float time() const { return time_; }
  // Spin the particles are currently integrated with (controls().spin applies on reset).
  float runSpin() const { return runSpin_; }
  SimBackend& backend() { return *backend_; }

 private:
  std::unique_ptr<SimBackend> backend_;
  std::vector<std::unique_ptr<Scenario>> scenarios_;
  size_t current_ = 0;
  SimControls controls_;
  float time_ = 0.0f;
  float runSpin_ = 0.0f;
  uint32_t frame_ = 0;
  uint32_t seed_ = 0x1234567u;
};

}  // namespace bh
