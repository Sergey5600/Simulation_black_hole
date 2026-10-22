#include "sim/Simulation.hpp"

#include <algorithm>

#include "core/Random.hpp"

namespace bh {

Simulation::Simulation(std::unique_ptr<SimBackend> backend) : backend_(std::move(backend)) {}

void Simulation::addScenario(std::unique_ptr<Scenario> scenario) {
  scenarios_.push_back(std::move(scenario));
}

void Simulation::select(size_t index) {
  current_ = std::min(index, scenarios_.size() - 1);
  const ScenarioDefaults d = current().defaults();
  controls_.timeScale = d.timeScale;
  controls_.timeMode = d.timeMode;
  reset();
}

void Simulation::reset() {
  time_ = 0.0f;
  frame_ = 0;
  seed_ = pcgHash(seed_ + 1u);
  backend_->resize(current().particleCount());
  current().reset(*backend_, seed_, time_);
}

void Simulation::update(float realDt) {
  if (controls_.paused) return;
  advance(realDt * controls_.timeScale);
}

void Simulation::advance(float simDt) {
  Scenario& sc = current();
  sc.update(*backend_, time_);

  StepParams sp;
  sp.dt = simDt;
  sp.timeMode = controls_.timeMode;
  sp.accuracy = controls_.accuracy;
  sc.configureStep(sp);

  ScenarioParams params = sc.params();
  params.runSeed = seed_;
  backend_->step(params, sp, pcgHash(seed_ ^ ++frame_), time_);
  time_ += simDt;
}

}  // namespace bh
