#pragma once

#include <cstddef>

#include "render/Camera.hpp"
#include "render/Renderer.hpp"
#include "sim/Journey.hpp"
#include "sim/Simulation.hpp"

namespace bh::ui {

struct UiState {
  bool showPanel = true;
  bool showHud = true;
};

struct FrameStats {
  float fps = 0.0f;
  float frameMs = 0.0f;
  float simMs = 0.0f;
  size_t alive = 0;
  size_t total = 0;
  float simTime = 0.0f;
  const char* backend = "";
};

// Everything the UI may read or edit. The UI never calls into GL or the
// backend directly; structural changes are returned as UiActions.
struct UiContext {
  Simulation& sim;
  Appearance& appearance;
  RenderSettings& render;
  OrbitCamera& camera;
  UiState& state;
  const Renderer& renderer;
  FrameStats stats;
  bool cudaAvailable = false;
  bool usingCuda = false;
};

struct UiActions {
  int selectScenario = -1;
  bool reset = false;
  bool resetView = false;
  bool resetAppearance = false;
  bool screenshot = false;
  int selectBackend = -1;  // 0 = CPU, 1 = CUDA
};

UiActions drawUi(UiContext& ctx);

// Physical readout of a recorded journey (--hud), bottom left of the frame.
// `solarMasses` turns geometric units into km, seconds and g; `exposureEv` is
// the camera's automatic exposure correction (NaN: not shown).
void drawJourneyHud(const JourneyFrame& frame, double solarMasses, float exposureEv);

}  // namespace bh::ui
