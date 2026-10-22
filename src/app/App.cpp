#include "app/App.hpp"

#include <glad/gl.h>
// glad must come before GLFW.
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string>

#include "backend/CpuBackend.hpp"
#include "render/Screenshot.hpp"
#include "sim/scenarios/AccretionDisk.hpp"
#include "sim/scenarios/PhotonWaves.hpp"
#include "ui/Theme.hpp"

namespace bh {

App::App(AppOptions options) : options_(std::move(options)) {}

App::~App() { shutdown(); }

bool App::init() {
  glfwSetErrorCallback([](int code, const char* msg) { std::fprintf(stderr, "[glfw] %d: %s\n", code, msg); });
  if (!glfwInit()) return false;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  window_ = glfwCreateWindow(options_.width, options_.height, "Black Hole", nullptr, nullptr);
  if (!window_) return false;
  glfwMakeContextCurrent(window_);
  glfwSwapInterval(options_.vsync && options_.screenshot.empty() ? 1 : 0);

  if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
    std::fprintf(stderr, "Failed to load OpenGL 4.3 functions\n");
    return false;
  }
  std::printf("OpenGL %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

  // Our callbacks first: ImGui chains to them.
  glfwSetWindowUserPointer(window_, this);
  glfwSetScrollCallback(window_, &App::onScroll);
  glfwSetKeyCallback(window_, &App::onKey);

  float xs = 1.0f, ys = 1.0f;
  glfwGetWindowContentScale(window_, &xs, &ys);
  dpiScale_ = std::max(1.0f, xs);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ui::applyTheme(dpiScale_);
  ImGui_ImplGlfw_InitForOpenGL(window_, true);
  ImGui_ImplOpenGL3_Init("#version 430");

  if (!renderer_.init()) return false;

  sim_ = std::make_unique<Simulation>(std::make_unique<CpuBackend>());
  sim_->addScenario(std::make_unique<AccretionDisk>());
  sim_->addScenario(std::make_unique<PhotonWaves>());
  for (size_t i = 0; i < sim_->scenarioCount(); ++i) appearances_.push_back(sim_->scenario(i).defaults().appearance);

  selectScenario(static_cast<size_t>(std::clamp(options_.scenario, 0, static_cast<int>(sim_->scenarioCount()) - 1)));
  applyParamOverrides();

  ViewPreset view = sim_->current().defaults().view;
  if (!std::isnan(options_.yaw)) view.yaw = options_.yaw;
  if (!std::isnan(options_.pitch)) view.pitch = options_.pitch;
  if (!std::isnan(options_.distance)) view.distance = options_.distance;
  camera_.setView(view, true);

  if (options_.warmup > 0.0f) {
    const float step = sim_->controls().timeScale / 60.0f;
    for (float t = 0.0f; t < options_.warmup; t += step) sim_->advance(step);
  }
  uiState_.showPanel = !options_.hideUi;
  return true;
}

void App::shutdown() {
  if (!window_) return;
  renderer_.shutdown();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window_);
  glfwTerminate();
  window_ = nullptr;
}

void App::selectScenario(size_t index) {
  sim_->select(index);
  camera_.setView(sim_->current().defaults().view, false);
}

void App::resetSimulation() { sim_->reset(); }

// --set "Label=value": tweak any scenario parameter from the command line
// (handy for scripted renders and screenshots).
void App::applyParamOverrides() {
  if (options_.params.empty()) return;
  for (const auto& [label, value] : options_.params) {
    bool found = false;
    for (ParamDesc& d : sim_->current().parameters()) {
      if (label != d.label) continue;
      found = true;
      try {
        switch (d.type) {
          case ParamDesc::Type::Float: *static_cast<float*>(d.value) = std::stof(value); break;
          case ParamDesc::Type::Int: *static_cast<int*>(d.value) = std::stoi(value); break;
          case ParamDesc::Type::Bool: *static_cast<bool*>(d.value) = value == "1" || value == "true"; break;
          case ParamDesc::Type::Enum: *static_cast<int*>(d.value) = std::stoi(value); break;
        }
      } catch (const std::exception&) {
        std::fprintf(stderr, "--set: bad value '%s' for '%s'\n", value.c_str(), label.c_str());
      }
    }
    if (!found) std::fprintf(stderr, "--set: no parameter named '%s' in '%s'\n", label.c_str(), sim_->current().name());
  }
  resetSimulation();
}

void App::applyActions(const ui::UiActions& a) {
  if (a.selectScenario >= 0) selectScenario(static_cast<size_t>(a.selectScenario));
  if (a.reset) resetSimulation();
  if (a.resetView) camera_.setView(sim_->current().defaults().view, false);
  if (a.resetAppearance) appearance() = sim_->current().defaults().appearance;
  if (a.screenshot) cleanScreenshotPending_ = true;
}

void App::handleMouse() {
  double x = 0.0, y = 0.0;
  glfwGetCursorPos(window_, &x, &y);
  const float dx = static_cast<float>(x - lastMouseX_);
  const float dy = static_cast<float>(y - lastMouseY_);
  lastMouseX_ = x;
  lastMouseY_ = y;

  const ImGuiIO& io = ImGui::GetIO();
  if (io.WantCaptureMouse) {
    pendingScroll_ = 0.0f;
    return;
  }
  if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) camera_.rotate(dx, dy);
  if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS ||
      glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
    int w = 0, h = 0;
    glfwGetWindowSize(window_, &w, &h);
    camera_.pan(dx, dy, static_cast<float>(h));
  }
  if (pendingScroll_ != 0.0f) camera_.zoom(pendingScroll_);
  pendingScroll_ = 0.0f;
}

void App::onScroll(GLFWwindow* window, double /*dx*/, double dy) {
  static_cast<App*>(glfwGetWindowUserPointer(window))->pendingScroll_ += static_cast<float>(dy);
}

void App::onKey(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
  if (action != GLFW_PRESS || ImGui::GetIO().WantCaptureKeyboard) return;
  App& app = *static_cast<App*>(glfwGetWindowUserPointer(window));
  switch (key) {
    case GLFW_KEY_SPACE: app.sim_->controls().paused = !app.sim_->controls().paused; break;
    case GLFW_KEY_R: app.resetSimulation(); break;
    case GLFW_KEY_H: app.uiState_.showPanel = !app.uiState_.showPanel; break;
    case GLFW_KEY_P: app.cleanScreenshotPending_ = true; break;
    case GLFW_KEY_F: app.camera_.autoRotate = !app.camera_.autoRotate; break;
    case GLFW_KEY_1: app.selectScenario(0); break;
    case GLFW_KEY_2: app.selectScenario(1); break;
    default: break;
  }
}

void App::takeScreenshot(const std::string& path, int width, int height) {
  if (saveScreenshot(path, width, height)) {
    std::printf("Saved %s\n", path.c_str());
  } else {
    std::fprintf(stderr, "Failed to save %s\n", path.c_str());
  }
}

int App::run() {
  if (!init()) {
    std::fprintf(stderr, "Initialization failed\n");
    return 1;
  }
  const bool capture = !options_.screenshot.empty();
  double last = glfwGetTime();
  float smoothedFrame = 1.0f / 60.0f;
  int frame = 0;
  glfwGetCursorPos(window_, &lastMouseX_, &lastMouseY_);

  while (!glfwWindowShouldClose(window_)) {
    glfwPollEvents();
    const double now = glfwGetTime();
    float dt = std::min(static_cast<float>(now - last), 0.1f);
    last = now;
    if (capture) dt = 1.0f / 60.0f;  // deterministic captures
    smoothedFrame += (dt - smoothedFrame) * 0.05f;

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(window_, &fbWidth, &fbHeight);
    if (fbWidth == 0 || fbHeight == 0) {  // minimized
      glfwWaitEvents();
      continue;
    }

    handleMouse();
    camera_.update(dt);
    sim_->update(dt);

    // Particles -> GL buffer (the CPU backend writes straight into mapped memory).
    const size_t count = sim_->backend().size();
    if (PointVertex* dst = renderer_.beginPointUpload(count)) sim_->backend().writeVertices(dst);
    renderer_.endPointUpload();

    FrameInput in;
    in.pointCount = count;
    in.photons = sim_->current().params().type == ScenarioType::PhotonWaves;
    in.appearance = appearance();
    in.lifetime = sim_->current().lifetime();
    in.view = camera_.view();
    in.projection = camera_.projection(static_cast<float>(fbWidth) / static_cast<float>(fbHeight));
    in.cameraPos = camera_.position();
    in.fovYRadians = glm::radians(camera_.fovDegrees);
    in.width = fbWidth;
    in.height = fbHeight;
    in.dpiScale = dpiScale_;
    renderer_.render(in, render_);

    // Images without UI: P key / button, or the final frame of a --hide-ui capture.
    const bool lastCaptureFrame = capture && frame + 1 >= options_.frames;
    if (cleanScreenshotPending_ || (lastCaptureFrame && options_.hideUi)) {
      takeScreenshot(capture ? options_.screenshot : makeScreenshotPath(), fbWidth, fbHeight);
      cleanScreenshotPending_ = false;
    }

    const BackendStats bs = sim_->backend().stats();
    stats_.fps = 1.0f / std::max(smoothedFrame, 1e-4f);
    stats_.frameMs = smoothedFrame * 1000.0f;
    stats_.simMs += (static_cast<float>(bs.stepMs) - stats_.simMs) * 0.1f;
    stats_.alive = bs.alive;
    stats_.total = count;
    stats_.simTime = sim_->time();
    stats_.backend = sim_->backend().name();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ui::UiContext ctx{*sim_, appearance(), render_, camera_, uiState_, renderer_, stats_};
    const ui::UiActions actions = ui::drawUi(ctx);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    applyActions(actions);

    ++frame;
    if (capture && frame >= options_.frames) {
      if (!options_.hideUi) takeScreenshot(options_.screenshot, fbWidth, fbHeight);
      break;
    }
    glfwSwapBuffers(window_);
  }
  return 0;
}

}  // namespace bh
