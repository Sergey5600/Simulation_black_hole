// CUDA backend. GL interop headers need <windows.h> first on Windows.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <cuda_gl_interop.h>
#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "backend/CudaBackend.hpp"

namespace bh {

namespace {

constexpr unsigned kBlock = 256;

bool check(cudaError_t err, const char* what) {
  if (err == cudaSuccess) return true;
  std::fprintf(stderr, "[cuda] %s: %s\n", what, cudaGetErrorString(err));
  return false;
}

unsigned blocksFor(size_t n) { return static_cast<unsigned>((n + kBlock - 1) / kBlock); }

__global__ void spawnKernel(Particle* particles, unsigned begin, unsigned count, ScenarioParams sc, unsigned seed,
                            float time, bool initial) {
  const unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) return;
  const unsigned index = begin + i;
  spawnParticle(sc, index, seed, time, initial, particles[index]);
}

__global__ void stepKernel(Particle* particles, unsigned n, ScenarioParams sc, StepParams sp, unsigned seed,
                           float time, unsigned* aliveCount) {
  const unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
  bool alive = false;
  if (i < n) {
    Particle p = particles[i];  // work in registers
    bool touched = true;
    if (!isAlive(p)) {
      if (sc.respawnDead) {
        spawnParticle(sc, i, seed, time, false, p);
      } else {
        touched = false;
      }
    }
    if (touched) {
      alive = advanceParticle(p, sp);
      particles[i] = p;
    }
  }
  // Warp-aggregated counter: one atomic per warp instead of one per particle.
  const unsigned mask = __ballot_sync(0xffffffffu, alive);
  if ((threadIdx.x & 31u) == 0u && mask) atomicAdd(aliveCount, static_cast<unsigned>(__popc(mask)));
}

__global__ void vertexKernel(const Particle* particles, unsigned n, PointVertex* out) {
  const unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = makeVertex(particles[i]);
}

}  // namespace

struct CudaBackend::Impl {
  Particle* particles = nullptr;
  size_t count = 0;
  size_t capacity = 0;
  unsigned* aliveCount = nullptr;

  PointVertex* staging = nullptr;  // fallback path when GL interop is unavailable
  size_t stagingCapacity = 0;

  cudaGraphicsResource* glResource = nullptr;
  unsigned glBuffer = 0;
  uint64_t glGeneration = ~0ull;
  bool interopFailed = false;

  BackendStats stats;
  std::string name;
};

bool CudaBackend::isAvailable(std::string* deviceName) {
  int devices = 0;
  if (cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0) {
    cudaGetLastError();  // clear the sticky "no device" error
    return false;
  }
  if (deviceName) {
    cudaDeviceProp prop{};
    cudaGetDeviceProperties(&prop, 0);
    *deviceName = prop.name;
  }
  return true;
}

CudaBackend::CudaBackend() : impl_(std::make_unique<Impl>()) {
  std::string device = "unknown device";
  isAvailable(&device);
  impl_->name = "CUDA \xC2\xB7 " + device;
  check(cudaMalloc(&impl_->aliveCount, sizeof(unsigned)), "cudaMalloc(alive)");
}

CudaBackend::~CudaBackend() {
  if (impl_->glResource) cudaGraphicsUnregisterResource(impl_->glResource);
  cudaFree(impl_->staging);
  cudaFree(impl_->particles);
  cudaFree(impl_->aliveCount);
}

const char* CudaBackend::name() const { return impl_->name.c_str(); }

size_t CudaBackend::size() const { return impl_->count; }

void CudaBackend::resize(size_t count) {
  Impl& m = *impl_;
  if (count > m.capacity) {
    cudaFree(m.particles);
    m.particles = nullptr;
    m.capacity = 0;
    if (!check(cudaMalloc(&m.particles, count * sizeof(Particle)), "cudaMalloc(particles)")) {
      m.count = 0;
      return;
    }
    m.capacity = count;
  }
  m.count = count;
  if (count) check(cudaMemset(m.particles, 0, count * sizeof(Particle)), "cudaMemset");  // all dead
  m.stats.alive = 0;
}

void CudaBackend::spawn(const ScenarioParams& sc, size_t begin, size_t count, uint32_t seed, float time,
                        bool initial) {
  Impl& m = *impl_;
  if (begin >= m.count) return;
  count = std::min(count, m.count - begin);
  if (count == 0) return;
  spawnKernel<<<blocksFor(count), kBlock>>>(m.particles, static_cast<unsigned>(begin), static_cast<unsigned>(count),
                                            sc, seed, time, initial);
  check(cudaGetLastError(), "spawnKernel");
}

void CudaBackend::step(const ScenarioParams& sc, const StepParams& sp, uint32_t seed, float time) {
  Impl& m = *impl_;
  const auto t0 = std::chrono::steady_clock::now();
  unsigned alive = 0;
  if (m.count) {
    cudaMemsetAsync(m.aliveCount, 0, sizeof(unsigned));
    stepKernel<<<blocksFor(m.count), kBlock>>>(m.particles, static_cast<unsigned>(m.count), sc, sp, seed, time,
                                               m.aliveCount);
    check(cudaGetLastError(), "stepKernel");
    check(cudaMemcpy(&alive, m.aliveCount, sizeof(unsigned), cudaMemcpyDeviceToHost), "alive count");  // syncs
  }
  m.stats.alive = alive;
  m.stats.stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

bool CudaBackend::writeVerticesGl(unsigned glBuffer, uint64_t generation) {
  Impl& m = *impl_;
  if (m.interopFailed || glBuffer == 0) return false;
  if (glBuffer != m.glBuffer || generation != m.glGeneration || !m.glResource) {
    if (m.glResource) cudaGraphicsUnregisterResource(m.glResource);
    m.glResource = nullptr;
    if (!check(cudaGraphicsGLRegisterBuffer(&m.glResource, glBuffer, cudaGraphicsRegisterFlagsWriteDiscard),
               "register GL buffer (falling back to host copies)")) {
      m.interopFailed = true;
      return false;
    }
    m.glBuffer = glBuffer;
    m.glGeneration = generation;
  }
  if (m.count == 0) return true;

  if (!check(cudaGraphicsMapResources(1, &m.glResource), "map GL buffer")) return false;
  void* ptr = nullptr;
  size_t bytes = 0;
  check(cudaGraphicsResourceGetMappedPointer(&ptr, &bytes, m.glResource), "mapped pointer");
  const bool fits = ptr && bytes >= m.count * sizeof(PointVertex);
  if (fits) {
    vertexKernel<<<blocksFor(m.count), kBlock>>>(m.particles, static_cast<unsigned>(m.count),
                                                 static_cast<PointVertex*>(ptr));
    check(cudaGetLastError(), "vertexKernel");
  }
  check(cudaGraphicsUnmapResources(1, &m.glResource), "unmap GL buffer");
  return fits;
}

void CudaBackend::writeVertices(PointVertex* dst) {
  Impl& m = *impl_;
  if (m.count == 0) return;
  if (m.count > m.stagingCapacity) {
    cudaFree(m.staging);
    m.staging = nullptr;
    m.stagingCapacity = 0;
    if (!check(cudaMalloc(&m.staging, m.count * sizeof(PointVertex)), "cudaMalloc(staging)")) return;
    m.stagingCapacity = m.count;
  }
  vertexKernel<<<blocksFor(m.count), kBlock>>>(m.particles, static_cast<unsigned>(m.count), m.staging);
  check(cudaMemcpy(dst, m.staging, m.count * sizeof(PointVertex), cudaMemcpyDeviceToHost), "download vertices");
}

BackendStats CudaBackend::stats() const { return impl_->stats; }

void CudaBackend::download(std::vector<Particle>& out) const {
  out.resize(impl_->count);
  if (impl_->count) {
    check(cudaMemcpy(out.data(), impl_->particles, impl_->count * sizeof(Particle), cudaMemcpyDeviceToHost),
          "download particles");
  }
}

}  // namespace bh
