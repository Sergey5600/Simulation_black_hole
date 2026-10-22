// CUDA backend vs CPU backend: same scenario, same seeds, compare the results.
// Skips (exit 0) when no CUDA device is present.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "backend/CpuBackend.hpp"
#include "backend/CudaBackend.hpp"

using namespace bh;

namespace {

// Fraction of particles whose CPU and GPU positions agree to `tol` (relative).
double agreement(const std::vector<Particle>& a, const std::vector<Particle>& b, float tol) {
  size_t same = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    if (isAlive(a[i]) != isAlive(b[i])) continue;
    const float d = length(a[i].x - b[i].x);
    if (d <= tol * std::max(1.0f, length(a[i].x))) ++same;
  }
  return a.empty() ? 1.0 : static_cast<double>(same) / static_cast<double>(a.size());
}

bool compare(const char* label, ScenarioParams sc, int steps, float dt) {
  CpuBackend cpu;
  CudaBackend gpu;
  const size_t n = 100000;
  cpu.resize(n);
  gpu.resize(n);
  sc.runSeed = 99u;
  cpu.spawn(sc, 0, n, 99u, 0.0f, true);
  gpu.spawn(sc, 0, n, 99u, 0.0f, true);
  StepParams sp;
  sp.dt = dt;
  for (int i = 0; i < steps; ++i) {
    cpu.step(sc, sp, 1000u + static_cast<uint32_t>(i), static_cast<float>(i) * dt);
    gpu.step(sc, sp, 1000u + static_cast<uint32_t>(i), static_cast<float>(i) * dt);
  }
  std::vector<Particle> g;
  gpu.download(g);
  // Fast-math and FMA make GPU results differ in the last bits; chaotic orbits
  // near the hole amplify that, so require broad rather than perfect agreement.
  const double a = agreement(cpu.particles(), g, 1e-2f);
  const long long da = static_cast<long long>(cpu.stats().alive) - static_cast<long long>(gpu.stats().alive);
  const bool ok = a > 0.97 && std::llabs(da) <= static_cast<long long>(n / 100);
  std::printf("[%s] %-14s %.2f%% of particles match, alive cpu=%zu gpu=%zu, step cpu=%.2f ms gpu=%.2f ms\n",
              ok ? " OK " : "FAIL", label, 100.0 * a, cpu.stats().alive, gpu.stats().alive, cpu.stats().stepMs,
              gpu.stats().stepMs);
  return ok;
}

}  // namespace

int main() {
  std::string device;
  if (!CudaBackend::isAvailable(&device)) {
    std::printf("No CUDA device found: skipping GPU tests.\n");
    return 0;
  }
  std::printf("Device: %s\n", device.c_str());
  int failures = 0;

  ScenarioParams disk;
  disk.type = ScenarioType::AccretionDisk;
  disk.respawnDead = true;
  failures += !compare("disk", disk, 60, 1.0f);

  ScenarioParams waves;
  waves.type = ScenarioType::PhotonWaves;
  waves.wave.photonsPerFront = 100000;
  failures += !compare("photon waves", waves, 60, 1.0f);

  std::printf("\n%s\n", failures ? "FAILED" : "ALL PASSED");
  return failures ? 1 : 0;
}
