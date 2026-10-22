#pragma once

#include <cstddef>
#include <cstdint>

#include "sim/Integrator.hpp"
#include "sim/Spawn.hpp"

namespace bh {

struct BackendStats {
  size_t alive = 0;
  double stepMs = 0.0;  // wall time of the last step()
};

// Owns the particle storage and runs the per-particle kernels.
// CpuBackend implements it with OpenMP; a CudaBackend (stage 3) will keep the
// particles in device memory and write vertices straight into the GL buffer.
class SimBackend {
 public:
  virtual ~SimBackend() = default;

  virtual const char* name() const = 0;

  // Resizes storage; all slots start dead.
  virtual void resize(size_t count) = 0;
  virtual size_t size() const = 0;

  // (Re)spawns slots [begin, begin + count).
  virtual void spawn(const ScenarioParams& sc, size_t begin, size_t count, uint32_t seed, float time,
                     bool initial) = 0;

  // Advances every live particle; refills dead slots if sc.respawnDead.
  virtual void step(const ScenarioParams& sc, const StepParams& sp, uint32_t seed, float time) = 0;

  // Writes one PointVertex per slot into `dst` (host memory, capacity >= size()).
  virtual void writeVertices(PointVertex* dst) = 0;

  // GPU backends: write the vertices straight into an OpenGL buffer object
  // (capacity >= size()). `generation` changes whenever the buffer storage is
  // reallocated. Returns false if unsupported; the caller then uses writeVertices().
  virtual bool writeVerticesGl(unsigned /*glBuffer*/, uint64_t /*generation*/) { return false; }

  virtual BackendStats stats() const = 0;
};

}  // namespace bh
