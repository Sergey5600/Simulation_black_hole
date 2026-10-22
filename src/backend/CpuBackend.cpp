#include "backend/CpuBackend.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace bh {

const char* CpuBackend::name() const {
#ifdef _OPENMP
  static char label[48];
  snprintf(label, sizeof(label), "CPU (OpenMP, %d threads)", omp_get_max_threads());
  return label;
#else
  return "CPU (single thread)";
#endif
}

void CpuBackend::resize(size_t count) {
  particles_.assign(count, Particle{});
  stats_.alive = 0;
}

void CpuBackend::spawn(const ScenarioParams& sc, size_t begin, size_t count, uint32_t seed, float time,
                       bool initial) {
  // int loop indices: MSVC's OpenMP 2.0 only accepts signed int counters.
  const int end = static_cast<int>(std::min(begin + count, particles_.size()));
#pragma omp parallel for schedule(static)
  for (int i = static_cast<int>(begin); i < end; ++i) {
    spawnParticle(sc, static_cast<uint32_t>(i), seed, time, initial, particles_[static_cast<size_t>(i)]);
  }
}

void CpuBackend::step(const ScenarioParams& sc, const StepParams& sp, uint32_t seed, float time) {
  const auto t0 = std::chrono::steady_clock::now();
  const int n = static_cast<int>(particles_.size());
  int alive = 0;

#pragma omp parallel for schedule(dynamic, 2048) reduction(+ : alive)
  for (int i = 0; i < n; ++i) {
    Particle& p = particles_[static_cast<size_t>(i)];
    if (!isAlive(p)) {
      if (!sc.respawnDead) continue;
      spawnParticle(sc, static_cast<uint32_t>(i), seed, time, false, p);
    }
    if (advanceParticle(p, sp)) ++alive;
  }

  stats_.alive = static_cast<size_t>(alive);
  stats_.stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void CpuBackend::writeVertices(PointVertex* dst) {
  const int n = static_cast<int>(particles_.size());
#pragma omp parallel for schedule(static)
  for (int i = 0; i < n; ++i) {
    dst[i] = makeVertex(particles_[static_cast<size_t>(i)]);
  }
}

}  // namespace bh
