#pragma once

#include <memory>
#include <string>
#include <vector>

#include "backend/SimBackend.hpp"

namespace bh {

// Particles live in GPU memory; the same BH_HD physics as the CPU backend runs
// as CUDA kernels (one thread per particle). Vertices are written straight
// into the renderer's OpenGL buffer through CUDA-GL interop.
// This header is plain C++ so non-CUDA translation units can include it.
class CudaBackend final : public SimBackend {
 public:
  // True if a CUDA-capable device and driver are present. Fills the device name.
  static bool isAvailable(std::string* deviceName = nullptr);

  CudaBackend();
  ~CudaBackend() override;

  const char* name() const override;

  void resize(size_t count) override;
  size_t size() const override;

  void spawn(const ScenarioParams& sc, size_t begin, size_t count, uint32_t seed, float time, bool initial) override;
  void step(const ScenarioParams& sc, const StepParams& sp, uint32_t seed, float time) override;
  void writeVertices(PointVertex* dst) override;
  bool writeVerticesGl(unsigned glBuffer, uint64_t generation) override;

  BackendStats stats() const override;

  // Copies the particle state back to the host (tests and tooling).
  void download(std::vector<Particle>& out) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace bh
