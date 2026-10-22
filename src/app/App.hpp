#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "render/Camera.hpp"
#include "render/Renderer.hpp"
#include "sim/Journey.hpp"
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
  bool classicDisk = false;  // bolometric T⁴ g⁴ disk instead of the exact visible spectrum

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
  std::string cameraPath = "flyby";  // a CameraPath or a first-person Journey
  std::string frameFormat;         // jpg or png (empty = preset)
  bool hud = false;                // physical readout on journey frames
  bool singularity = true;         // the story's artistic finale (--no-singularity)
  int autoExposure = -1;           // -1: on for first-person journeys only; 0 off; 1 on
  std::string layers;              // comma list of compositing layers to write (see --layers)
  std::string layerFormat = "exr"; // exr (linear) or png (tone-mapped)
  bool vdb = false;                // also write the particle volume per frame (DIR/vdb/)
  double solarMasses = 4.3e6;      // hole mass for the readout's units (Sagittarius A*)
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
  void setJourneyCamera(FrameInput& in, const ViewPreset& base, const JourneyFrame& f);
  void takeScreenshot(const std::string& path, int width, int height);
  // Saves the particles' light as an OpenVDB file (grids "density" and "color").
  bool exportVdb(const std::string& path, FrameInput in, const RenderSettings& settings);
  // Draws ImGui content (e.g. the journey readout) on top of an off-screen target.
  void drawOverlay(const gl::RenderTarget& target, const std::function<void()>& draw);

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
  bool vdbPending_ = false;
};

}  // namespace bh
