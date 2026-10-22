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
  std::string backend = "auto";  // "auto" (CUDA if available), "cpu" or "cuda"
  std::vector<std::pair<std::string, std::string>> params;  // --set "Label=value"
  float yaw = NAN, pitch = NAN, distance = NAN;             // optional camera override
  bool lensing = false;
  float spin = 0.0f;
  float rayStep = 0.0f;  // > 0 overrides the ray-tracing step
  int lensSource = -1;   // -1: scenario default; else Appearance::LensSource

  // Video recording (--record): numbered frames + make_video.bat, then exit.
  std::string recordDir;
  int recordWidth = 3840;
  int recordHeight = 2160;
  float recordSeconds = 60.0f;
  int recordFps = 60;
  std::string quality = "film";    // preset: draft, film, max (explicit options below override it)
  int ssaa = 0;                    // supersampling: render at ssaa x the size, box-filter down (0 = preset)
  int samples = 0;                 // jittered sub-frames per frame (anti-aliasing + motion blur; 0 = preset)
  float shutter = -1.0f;           // motion blur: fraction of the frame interval (< 0 = preset)
  std::string cameraPath = "flyby";
  std::string frameFormat;         // jpg or png (empty = preset)
};

class App {
 public:
  explicit App(AppOptions options);
  ~App();
  int run();

 private:
  bool init();
  void shutdown();

  std::unique_ptr<SimBackend> makeBackend(bool cuda);
  void selectBackend(bool cuda);
  void selectScenario(size_t index);
  void resetSimulation();
  void applyParamOverrides();
  void applyActions(const ui::UiActions& actions);
  void handleMouse();
  void uploadPoints();
  FrameInput makeFrameInput(int width, int height);
  int record();
  void takeScreenshot(const std::string& path, int width, int height);

  Appearance& appearance() { return appearances_[sim_->currentIndex()]; }

  static void onScroll(GLFWwindow* window, double dx, double dy);
  static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);

  AppOptions options_;
  GLFWwindow* window_ = nullptr;
  float dpiScale_ = 1.0f;

  std::unique_ptr<Simulation> sim_;
  bool cudaAvailable_ = false;
  bool usingCuda_ = false;
  std::string cudaDevice_;
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
