#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "render/Camera.hpp"
#include "render/Renderer.hpp"
#include "sim/Simulation.hpp"
#include "ui/Ui.hpp"

struct GLFWwindow;

namespace bh {

struct AppOptions {
  int width = 1600;
  int height = 900;
  int scenario = 0;
  float warmup = 0.0f;       // simulated time (M/c) to run before the first frame
  std::string screenshot;    // non-empty: render `frames` frames, save PNG, exit
  int frames = 4;
  bool hideUi = false;
  bool vsync = true;
  std::vector<std::pair<std::string, std::string>> params;  // --set "Label=value"
  float yaw = NAN, pitch = NAN, distance = NAN;             // optional camera override
};

class App {
 public:
  explicit App(AppOptions options);
  ~App();
  int run();

 private:
  bool init();
  void shutdown();

  void selectScenario(size_t index);
  void resetSimulation();
  void applyParamOverrides();
  void applyActions(const ui::UiActions& actions);
  void handleMouse();
  void takeScreenshot(const std::string& path, int width, int height);

  Appearance& appearance() { return appearances_[sim_->currentIndex()]; }

  static void onScroll(GLFWwindow* window, double dx, double dy);
  static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);

  AppOptions options_;
  GLFWwindow* window_ = nullptr;
  float dpiScale_ = 1.0f;

  std::unique_ptr<Simulation> sim_;
  Renderer renderer_;
  OrbitCamera camera_;
  RenderSettings render_;
  std::vector<Appearance> appearances_;  // one per scenario
  ui::UiState uiState_;
  ui::FrameStats stats_;

  float pendingScroll_ = 0.0f;
  double lastMouseX_ = 0.0, lastMouseY_ = 0.0;
  bool cleanScreenshotPending_ = false;
};

}  // namespace bh
