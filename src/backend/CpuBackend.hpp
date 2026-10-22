#pragma once

#include <vector>

#include "backend/SimBackend.hpp"

namespace bh {

class CpuBackend final : public SimBackend {
 public:
  const char* name() const override;

  void resize(size_t count) override;
  size_t size() const override { return particles_.size(); }

  void spawn(const ScenarioParams& sc, size_t begin, size_t count, uint32_t seed, float time, bool initial) override;
  void step(const ScenarioParams& sc, const StepParams& sp, uint32_t seed, float time) override;
  void writeVertices(PointVertex* dst) override;

  BackendStats stats() const override { return stats_; }

  // Direct access for tests and tooling.
  std::vector<Particle>& particles() { return particles_; }

 private:
  std::vector<Particle> particles_;
  BackendStats stats_;
};

}  // namespace bh
