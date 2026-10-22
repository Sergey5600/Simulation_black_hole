#pragma once

// BH_HD marks functions that must compile both for the CPU and for CUDA
// kernels. The whole physics core uses it, so the CUDA backend (stage 3) can
// include these headers unchanged.
#if defined(__CUDACC__)
#define BH_HD __host__ __device__
#define BH_INLINE __forceinline__
#else
#define BH_HD
#define BH_INLINE inline
#endif
