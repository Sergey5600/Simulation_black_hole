#pragma once

#include <cmath>
#include <cstdint>

#include "core/Config.hpp"

namespace bh {

// PCG hash (Jarzynski & Olano 2020): stateless, so any particle can derive its
// own random stream from (index, seed) on any thread or GPU lane.
BH_HD BH_INLINE uint32_t pcgHash(uint32_t v) {
  const uint32_t state = v * 747796405u + 2891336453u;
  const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

struct Rng {
  uint32_t state;

  BH_HD explicit Rng(uint32_t index, uint32_t seed) : state(pcgHash(index ^ pcgHash(seed))) {}

  // Uniform in [0, 1).
  BH_HD float uniform() {
    state = pcgHash(state);
    return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
  }
  BH_HD float uniform(float lo, float hi) { return lo + (hi - lo) * uniform(); }

  // Standard normal via Box-Muller.
  BH_HD float gaussian() {
    const float u1 = fmaxf(uniform(), 1e-7f);
    const float u2 = uniform();
    return sqrtf(-2.0f * logf(u1)) * cosf(6.28318530718f * u2);
  }
};

}  // namespace bh
