cmake_minimum_required(VERSION 3.21)
project(BlackHoleSim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BH_BUILD_TESTS "Build physics unit tests" ON)
option(BH_ENABLE_CUDA "Build the CUDA backend when the CUDA Toolkit is installed" ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Dependencies)
include(EmbedShaders)

# ---------------------------------------------------------------------------
# Physics core: header-only, no graphics dependencies. Everything in here is
# written as BH_HD (host+device) so the CUDA backend can reuse it verbatim.
# ---------------------------------------------------------------------------
add_library(bh_core INTERFACE)
target_include_directories(bh_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# Simulation: scenarios + compute backends (CPU/OpenMP for now, CUDA next).
# ---------------------------------------------------------------------------
add_library(bh_sim STATIC
  src/sim/Simulation.cpp
  src/sim/Journey.cpp
  src/sim/scenarios/AccretionDisk.cpp
  src/sim/scenarios/PhotonWaves.cpp
  src/sim/scenarios/StarDisruption.cpp
  src/backend/CpuBackend.cpp
)
target_link_libraries(bh_sim PUBLIC bh_core)
if(OpenMP_CXX_FOUND)
  target_link_libraries(bh_sim PUBLIC OpenMP::OpenMP_CXX)
endif()

# ---------------------------------------------------------------------------
# CUDA backend (optional): same BH_HD physics, compiled as GPU kernels.
# ---------------------------------------------------------------------------
set(BH_HAS_CUDA OFF)
if(BH_ENABLE_CUDA)
  find_package(CUDAToolkit QUIET)
  if(CUDAToolkit_FOUND)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
      # Turing (RTX 20), Ampere (RTX 30), Ada (RTX 40). Override with -DCMAKE_CUDA_ARCHITECTURES=...
      set(CMAKE_CUDA_ARCHITECTURES 75 86 89)
    endif()
    if(MSVC)
      # Brand-new Visual Studio versions are often newer than nvcc's official
      # support list. CUDAFLAGS is read by the compiler check in enable_language().
      set(ENV{CUDAFLAGS} "$ENV{CUDAFLAGS} -allow-unsupported-compiler")
    endif()
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 20)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(BH_HAS_CUDA ON)

    add_library(bh_cuda STATIC src/backend/CudaBackend.cu)
    target_link_libraries(bh_cuda PUBLIC bh_core CUDA::cudart_static)
    target_compile_definitions(bh_cuda PUBLIC BH_HAS_CUDA)
    target_compile_options(bh_cuda PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math -lineinfo>)
    if(MSVC)
      target_compile_options(bh_cuda PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>)
    endif()
    message(STATUS "CUDA backend: ON (nvcc ${CMAKE_CUDA_COMPILER_VERSION}, archs ${CMAKE_CUDA_ARCHITECTURES})")
  else()
    message(STATUS "CUDA backend: OFF (CUDA Toolkit not found, building the CPU backend only)")
  endif()
endif()

# ---------------------------------------------------------------------------
# Application: window, renderer, UI.
# ---------------------------------------------------------------------------
file(GLOB BH_SHADER_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/*.glsl)
bh_embed_shaders(${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedShaders.hpp ${BH_SHADER_FILES})

add_executable(blackhole
  src/main.cpp
  src/app/App.cpp
  src/render/GlUtil.cpp
  src/render/Camera.cpp
  src/render/CameraPath.cpp
  src/render/Renderer.cpp
  src/render/Screenshot.cpp
  src/ui/Theme.cpp
  src/ui/Widgets.cpp
  src/ui/Ui.cpp
)
target_include_directories(blackhole PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_include_directories(blackhole SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_link_libraries(blackhole PRIVATE bh_sim glad glfw glm::glm imgui tinyexr)
if(BH_HAS_CUDA)
  target_link_libraries(blackhole PRIVATE bh_cuda)
endif()

if(MSVC)
  # /utf-8: sources contain Greek letters and math symbols in UI strings.
  target_compile_options(bh_sim PRIVATE /W4 /utf-8)
  target_compile_options(blackhole PRIVATE /W4 /utf-8)
  # Standard sscanf/snprintf are fine here; silence MSVC's "_s variant" advice.
  target_compile_definitions(blackhole PRIVATE _CRT_SECURE_NO_WARNINGS)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT blackhole)
else()
  target_compile_options(bh_sim PRIVATE -Wall -Wextra -O3)
  target_compile_options(blackhole PRIVATE -Wall -Wextra)
endif()

if(BH_BUILD_TESTS)
  enable_testing()
  add_executable(bh_tests tests/test_physics.cpp)
  target_link_libraries(bh_tests PRIVATE bh_sim)
  add_test(NAME physics COMMAND bh_tests)
  if(BH_HAS_CUDA)
    add_executable(bh_cuda_tests tests/test_cuda.cpp)
    target_link_libraries(bh_cuda_tests PRIVATE bh_sim bh_cuda)
    add_test(NAME cuda COMMAND bh_cuda_tests)
  endif()
endif()
