cmake_minimum_required(VERSION 3.21)
project(BlackHoleSim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BH_BUILD_TESTS "Build physics unit tests" ON)

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
# Application: window, renderer, UI.
# ---------------------------------------------------------------------------
file(GLOB BH_SHADER_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/*.glsl)
bh_embed_shaders(${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedShaders.hpp ${BH_SHADER_FILES})

add_executable(blackhole
  src/main.cpp
  src/app/App.cpp
  src/render/GlUtil.cpp
  src/render/Camera.cpp
  src/render/Renderer.cpp
  src/render/Screenshot.cpp
  src/ui/Theme.cpp
  src/ui/Widgets.cpp
  src/ui/Ui.cpp
)
target_include_directories(blackhole PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_include_directories(blackhole SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_link_libraries(blackhole PRIVATE bh_sim glad glfw glm::glm imgui)

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
endif()
