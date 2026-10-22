#pragma once

#include <cstddef>

#include "render/Camera.hpp"
#include "render/Renderer.hpp"
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
};

struct UiActions {
  int selectScenario = -1;
  bool reset = false;
  bool resetView = false;
  bool resetAppearance = false;
  bool screenshot = false;
};

UiActions drawUi(UiContext& ctx);

}  // namespace bh::ui
