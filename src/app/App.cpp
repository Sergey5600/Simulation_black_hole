#include "app/App.hpp"

#include <glad/gl.h>
// glad must come before GLFW.
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "backend/CpuBackend.hpp"
#ifdef BH_HAS_CUDA
#include "backend/CudaBackend.hpp"
#endif
#include "render/CameraPath.hpp"
#include "render/Screenshot.hpp"
#include "sim/scenarios/AccretionDisk.hpp"
#include "sim/scenarios/PhotonWaves.hpp"
#include "sim/scenarios/StarDisruption.hpp"
#include "ui/Theme.hpp"
#include "ui/Ui.hpp"

namespace bh {

App::App(AppOptions options) : options_(std::move(options)) {}

App::~App() { shutdown(); }

bool App::init() {
  glfwSetErrorCallback([](int code, const char* msg) { std::fprintf(stderr, "[glfw] %d: %s\n", code, msg); });
  if (!glfwInit()) return false;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);  // 4.4+: glClearTexImage for the particle volume
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  window_ = glfwCreateWindow(options_.width, options_.height, "Black Hole", nullptr, nullptr);
  if (!window_) return false;
  glfwMakeContextCurrent(window_);
  glfwSwapInterval(options_.vsync && options_.screenshot.empty() ? 1 : 0);

  if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
    std::fprintf(stderr, "Failed to load OpenGL 4.5 functions\n");
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

#ifdef BH_HAS_CUDA
  cudaAvailable_ = CudaBackend::isAvailable(&cudaDevice_);
#endif
  const bool wantCuda = options_.backend == "cuda" || (options_.backend == "auto" && cudaAvailable_);
  if (wantCuda && !cudaAvailable_) std::fprintf(stderr, "CUDA backend not available, using CPU\n");
  usingCuda_ = wantCuda && cudaAvailable_;
  sim_ = std::make_unique<Simulation>(makeBackend(usingCuda_));
  std::printf("Compute backend: %s\n", sim_->backend().name());
  sim_->addScenario(std::make_unique<AccretionDisk>());
  sim_->addScenario(std::make_unique<PhotonWaves>());
  sim_->addScenario(std::make_unique<StarDisruption>());
  for (size_t i = 0; i < sim_->scenarioCount(); ++i) appearances_.push_back(sim_->scenario(i).defaults().appearance);
  sim_->controls().spin = std::clamp(options_.spin, -0.998f, 0.998f);

  // The GPU handles far more particles: start every scenario denser.
  if (usingCuda_) {
    const std::pair<const char*, int> gpuCounts[] = {{"Particles", 2000000}, {"Photons per front", 16384}};
    for (size_t i = 0; i < sim_->scenarioCount(); ++i) {
      for (ParamDesc& d : sim_->scenario(i).parameters()) {
        for (const auto& [label, count] : gpuCounts) {
          if (d.type == ParamDesc::Type::Int && std::string(d.label) == label) *static_cast<int*>(d.value) = count;
        }
      }
    }
  }
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
  render_.lensing = options_.lensing;
  if (options_.lensSource >= 0) appearance().lensSource = options_.lensSource;
  if (options_.rayStep > 0.0f) render_.rayStepScale = options_.rayStep;
  render_.diskVisibleSpectrum = !options_.classicDisk;
  return true;
}

void App::shutdown() {
  if (!window_) return;
  sim_.reset();  // GPU backends unregister GL buffers: do it while they still exist
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

std::unique_ptr<SimBackend> App::makeBackend(bool cuda) {
#ifdef BH_HAS_CUDA
  if (cuda) return std::make_unique<CudaBackend>();
#else
  (void)cuda;
#endif
  return std::make_unique<CpuBackend>();
}

void App::selectBackend(bool cuda) {
  if (cuda == usingCuda_ || (cuda && !cudaAvailable_)) return;
  usingCuda_ = cuda;
  sim_->setBackend(makeBackend(cuda));
}

void App::applyActions(const ui::UiActions& a) {
  if (a.selectScenario >= 0) selectScenario(static_cast<size_t>(a.selectScenario));
  if (a.reset) resetSimulation();
  if (a.resetView) camera_.setView(sim_->current().defaults().view, false);
  if (a.resetAppearance) appearance() = sim_->current().defaults().appearance;
  if (a.screenshot) cleanScreenshotPending_ = true;
  if (a.selectBackend >= 0) selectBackend(a.selectBackend == 1);
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
    case GLFW_KEY_L: app.render_.lensing = !app.render_.lensing; break;
    case GLFW_KEY_1: app.selectScenario(0); break;
    case GLFW_KEY_2: app.selectScenario(1); break;
    case GLFW_KEY_3: app.selectScenario(2); break;
    default: break;
  }
}

void App::drawOverlay(const gl::RenderTarget& target, const std::function<void()>& draw) {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(target.width), static_cast<float>(target.height));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  io.DeltaTime = 1.0f / 60.0f;
  ImGui_ImplOpenGL3_NewFrame();
  // Auto-sized windows measure themselves during their first frame and stay
  // hidden in it: lay out once without drawing.
  ImGui::NewFrame();
  draw();
  ImGui::EndFrame();
  ImGui::NewFrame();
  draw();
  ImGui::Render();
  glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void App::takeScreenshot(const std::string& path, int width, int height) {
  if (saveScreenshot(path, width, height)) {
    std::printf("Saved %s\n", path.c_str());
  } else {
    std::fprintf(stderr, "Failed to save %s\n", path.c_str());
  }
}

void App::uploadPoints() {
  // GPU backends write into the GL buffer directly; otherwise map it and fill it on the CPU.
  const size_t count = sim_->backend().size();
  const Renderer::PointBuffer points = renderer_.reservePoints(count);
  if (!sim_->backend().writeVerticesGl(points.id, points.generation)) {
    if (PointVertex* dst = renderer_.beginPointUpload(count)) sim_->backend().writeVertices(dst);
    renderer_.endPointUpload();
  }
}

FrameInput App::makeFrameInput(int width, int height) {
  FrameInput in;
  in.pointCount = sim_->backend().size();
  in.photons = sim_->current().params().type == ScenarioType::PhotonWaves;
  in.scalarIsBrightness = sim_->current().tagIsBrightness();
  in.appearance = appearance();
  in.lifetime = sim_->current().lifetime();
  in.view = camera_.view();
  in.projection = camera_.projection(static_cast<float>(width) / static_cast<float>(height));
  in.cameraPos = camera_.position();
  in.fovYRadians = glm::radians(camera_.fovDegrees);
  in.width = width;
  in.height = height;
  in.dpiScale = dpiScale_;
  in.time = sim_->time();
  return in;
}

// Offline video. Each output frame averages `samples` sub-frames, each shifted
// by a sub-pixel jitter (Halton 2,3) and spread over the shutter interval, which
// gives anti-aliasing and motion blur at once; every sub-frame is rendered at
// ssaa x the output size. Frames are box-filtered down on the GPU and written as
// frame_NNNNN.jpg/png. Existing frames are skipped, so an interrupted recording
// resumes where it stopped (the simulation still runs through them to stay in sync).
namespace {

float halton(int index, int base) {
  float f = 1.0f, r = 0.0f;
  for (int i = index; i > 0; i /= base) {
    f /= static_cast<float>(base);
    r += f * static_cast<float>(i % base);
  }
  return r;
}

struct QualityPreset {
  int ssaa, samples;
  float shutter, rayStep;
  int raySteps;
  const char* format;
};

bool qualityPreset(const std::string& name, QualityPreset& q) {
  if (name == "draft") q = {1, 1, 0.0f, 0.03f, 1500, "jpg"};
  else if (name == "film") q = {2, 1, 0.0f, 0.012f, 4000, "jpg"};
  else if (name == "max") q = {1, 9, 0.5f, 0.006f, 8000, "jpg"};
  else return false;
  return true;
}

}  // namespace

// First-person journey camera on the radial line `base.yaw`/`base.pitch`
// (`base.pitch` above the disk plane). The gaze turns in the plane of that line
// and the horizontal side direction: 0 looks at the hole, π straight out.
void App::setJourneyCamera(FrameInput& in, const ViewPreset& base, const JourneyFrame& f) {
  const float cp = std::cos(base.pitch);
  const glm::vec3 radial(cp * std::sin(base.yaw), std::sin(base.pitch), cp * std::cos(base.yaw));
  const glm::vec3 pos = radial * f.r;
  const glm::vec3 side = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), radial));
  const glm::vec3 up = glm::normalize(glm::cross(radial, side));  // perpendicular to every gaze
  const glm::vec3 forward = glm::normalize(-radial * std::cos(f.gaze) + side * std::sin(f.gaze));
  const float fov = glm::radians(75.0f);
  const float aspect = static_cast<float>(in.width) / static_cast<float>(in.height);
  in.view = glm::lookAt(pos, pos + forward, up);
  in.projection = glm::perspective(fov, aspect, 0.01f, 5000.0f);  // sub-pixel jitter is added afterwards
  in.cameraPos = pos;
  in.fovYRadians = fov;
  in.falling = !f.hovering;
  in.fallEnergy = f.energy;
}

int App::record() {
  CameraPath path;
  ViewPreset base = sim_->current().defaults().view;
  if (!std::isnan(options_.yaw)) base.yaw = options_.yaw;
  if (!std::isnan(options_.pitch)) base.pitch = options_.pitch;
  if (!std::isnan(options_.distance)) base.distance = options_.distance;
  Journey journey;
  const bool firstPerson = Journey::named(options_.cameraPath, base.distance, journey);
  if (!firstPerson && !CameraPath::named(options_.cameraPath, base, path)) {
    std::fprintf(stderr, "Unknown camera path '%s' (available: %s, %s)\n", options_.cameraPath.c_str(),
                 CameraPath::names(), Journey::names());
    return 2;
  }
  QualityPreset q{};
  if (!qualityPreset(options_.quality, q)) {
    std::fprintf(stderr, "Unknown quality '%s' (draft, film, max)\n", options_.quality.c_str());
    return 2;
  }
  // Explicit options override the preset.
  const int ssaa = std::clamp(options_.ssaa > 0 ? options_.ssaa : q.ssaa, 1, 4);
  const int samples = std::clamp(options_.samples > 0 ? options_.samples : q.samples, 1, 64);
  const float shutter = std::clamp(options_.shutter >= 0.0f ? options_.shutter : q.shutter, 0.0f, 1.0f);
  const std::string format = options_.frameFormat.empty() ? q.format : options_.frameFormat;

  const int outW = options_.recordWidth, outH = options_.recordHeight;
  const int bigW = outW * ssaa, bigH = outH * ssaa;
  const int frames = std::max(1, static_cast<int>(std::lround(options_.recordSeconds * static_cast<float>(options_.recordFps))));
  const float simDt = sim_->controls().timeScale / static_cast<float>(options_.recordFps);

  render_.spin = sim_->runSpin();
  RenderSettings settings = render_;
  settings.rayTileSize = 256;
  if (firstPerson) {
    const char* name = options_.cameraPath.c_str();
    if (!settings.lensing) std::printf("%s: turning gravitational lensing on\n", name);
    if (settings.spin != 0.0f) std::printf("%s: the first-person camera uses a non-spinning hole (spin set to 0)\n", name);
    settings.lensing = true;
    settings.spin = 0.0f;
  }
  const bool hud = options_.hud && firstPerson;
  if (options_.hud && !firstPerson) std::printf("--hud: only first-person journeys (%s) have a readout\n", Journey::names());
  if (hud) {
    // Fonts sized for the output frame; the UI backend rebuilds its atlas on the next frame.
    ui::applyTheme(std::max(0.8f, 1.25f * static_cast<float>(outH) / 1080.0f));
    ImGui_ImplOpenGL3_DestroyFontsTexture();
  }
  if (options_.rayStep <= 0.0f) {
    settings.rayStepScale = q.rayStep;
    settings.rayMaxSteps = q.raySteps;
  }

  gl::RenderTarget target, accum, out;
  target.create(bigW, bigH, GL_RGBA8, false);
  if (samples > 1) accum.create(bigW, bigH, GL_RGBA32F, false);
  out.create(outW, outH, GL_RGBA8, false);
  std::vector<unsigned char> pixels(static_cast<size_t>(outW) * outH * 3);
  std::filesystem::create_directories(options_.recordDir);
  glfwSwapInterval(0);

  std::printf("Recording %d frames: %dx%d, quality '%s' (%dx supersampling, %d samples, shutter %.2f, ray step %.4f), "
              "path '%s' -> %s\n",
              frames, outW, outH, options_.quality.c_str(), ssaa, samples, shutter, settings.rayStepScale,
              options_.cameraPath.c_str(), options_.recordDir.c_str());
  // Sub-frame k of frame f into `target` (HDR result stays in the renderer for metering).
  auto renderSubframe = [&](int f, int k) {
    // Sub-frame time inside the shutter, in frames (centered on the frame).
    const float offset = samples > 1 ? ((static_cast<float>(k) + 0.5f) / static_cast<float>(samples) - 0.5f) * shutter : 0.0f;
    const float u = frames > 1 ? (static_cast<float>(f) + offset) / static_cast<float>(frames - 1) : 0.0f;
    if (!firstPerson) camera_.setView(path.evaluate(std::clamp(u, 0.0f, 1.0f)), true);

    FrameInput in = makeFrameInput(bigW, bigH);
    if (firstPerson) setJourneyCamera(in, base, journey.at(std::clamp(u, 0.0f, 1.0f)));
    in.dpiScale = dpiScale_ * static_cast<float>(ssaa);
    in.time += offset * simDt;
    if (samples > 1) {  // sub-pixel jitter: shift the projection by a fraction of a pixel
      in.projection[2][0] += (halton(k + 1, 2) - 0.5f) * 2.0f / static_cast<float>(bigW);
      in.projection[2][1] += (halton(k + 1, 3) - 0.5f) * 2.0f / static_cast<float>(bigH);
    }
    renderer_.render(in, settings, target.fbo);
  };

  // Auto exposure, like a video camera: meter the highlights of each frame and
  // follow them smoothly (time constant 0.4 s). Brightens by at most 2 EV, so
  // a fading, redshifted sky still reads as fading.
  const bool autoExposure = options_.autoExposure < 0 ? firstPerson : options_.autoExposure > 0;
  const float baseExposure = settings.exposure;
  float exposureEv = 0.0f;
  bool metered = false;
  auto meteredEv = [&] {
    constexpr float kHighlight = 2.5f;  // the 99.5% brightest light lands here (ACES shoulder)
    const float highlights = std::max(renderer_.meterLuminance(0.995f) * baseExposure, 1e-8f);
    return std::clamp(std::log2(kHighlight / highlights), -20.0f, 2.0f);
  };

  const double start = glfwGetTime();
  int rendered = 0;
  for (int f = 0; f < frames && !glfwWindowShouldClose(window_); ++f) {
    glfwPollEvents();
    if (f > 0) sim_->advance(simDt);

    char name[64];
    std::snprintf(name, sizeof(name), "frame_%05d.%s", f, format.c_str());
    const std::string file = (std::filesystem::path(options_.recordDir) / name).string();
    if (std::filesystem::exists(file)) continue;  // resume

    uploadPoints();
    if (autoExposure && !metered) {  // first frame (or first after a resume): meter before shooting
      renderSubframe(f, 0);
      exposureEv = meteredEv();
      metered = true;
    }
    settings.exposure = baseExposure * std::exp2(exposureEv);
    if (samples > 1) {
      glBindFramebuffer(GL_FRAMEBUFFER, accum.fbo);
      glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
      glClear(GL_COLOR_BUFFER_BIT);
    }
    for (int k = 0; k < samples; ++k) {
      renderSubframe(f, k);
      if (samples > 1) renderer_.accumulate(target.color, accum, 1.0f / static_cast<float>(samples));
    }
    const float frameEv = exposureEv;
    if (autoExposure) exposureEv += (meteredEv() - exposureEv) * (1.0f - std::exp(-1.0f / (0.4f * static_cast<float>(options_.recordFps))));
    renderer_.downsample(samples > 1 ? accum.color : target.color, ssaa, out);
    if (hud) {
      const float u = frames > 1 ? static_cast<float>(f) / static_cast<float>(frames - 1) : 0.0f;
      drawOverlay(out, [&] { ui::drawJourneyHud(journey.at(u), options_.solarMasses, autoExposure ? frameEv : NAN); });
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, out.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, outW, outH, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    if (!writeImage(file, outW, outH, pixels.data())) {
      std::fprintf(stderr, "Failed to write %s (disk full?)\n", file.c_str());
      return 1;
    }
    ++rendered;

    // Preview in the window, letterboxed.
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window_, &fbW, &fbH);
    if (fbW > 0 && fbH > 0) {
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      const float scale = std::min(static_cast<float>(fbW) / outW, static_cast<float>(fbH) / outH);
      const int w = static_cast<int>(outW * scale), h = static_cast<int>(outH * scale);
      const int x0 = (fbW - w) / 2, y0 = (fbH - h) / 2;
      glBlitFramebuffer(0, 0, outW, outH, x0, y0, x0 + w, y0 + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
      glfwSwapBuffers(window_);
    }

    const double elapsed = glfwGetTime() - start;
    const double perFrame = elapsed / rendered;
    const double eta = perFrame * (frames - 1 - f);
    char title[160];
    std::snprintf(title, sizeof(title), "Black Hole \xE2\x80\x94 recording %d/%d \xC2\xB7 %.1f s/frame \xC2\xB7 ETA %dh %02dm",
                  f + 1, frames, perFrame, static_cast<int>(eta / 3600), static_cast<int>(eta / 60) % 60);
    glfwSetWindowTitle(window_, title);
    if (rendered == 1 || f % 50 == 0) std::printf("%s\n", title + 15);
  }
  target.destroy();
  accum.destroy();
  out.destroy();

  // ffmpeg helper next to the frames: H.264 plays everywhere; the HEVC 10-bit
  // line gives smaller files and smoother dark gradients.
  const std::string input = " -framerate " + std::to_string(options_.recordFps) + " -i frame_%05d." + format;
  const std::string h264 = "ffmpeg -y" + input + " -c:v libx264 -preset slow -crf 14 -pix_fmt yuv420p blackhole.mp4";
  const std::string hevc =
      "ffmpeg -y" + input + " -c:v libx265 -preset slow -crf 16 -pix_fmt yuv420p10le -tag:v hvc1 blackhole_hevc.mp4";
  {
    std::ofstream bat(std::filesystem::path(options_.recordDir) / "make_video.bat");
    bat << "@echo off\r\ncd /d \"%~dp0\"\r\n" << h264 << "\r\nrem Higher quality, smaller file (HEVC 10-bit):\r\nrem " << hevc
        << "\r\npause\r\n";
    std::ofstream sh(std::filesystem::path(options_.recordDir) / "make_video.sh");
    sh << "#!/bin/sh\ncd \"$(dirname \"$0\")\"\n" << h264 << "\n# HEVC 10-bit:\n# " << hevc << "\n";
  }
  std::printf("Done: %d new frames in %.1f min. Make the video with make_video.bat in %s\n", rendered,
              (glfwGetTime() - start) / 60.0, options_.recordDir.c_str());
  return 0;
}

int App::run() {
  if (!init()) {
    std::fprintf(stderr, "Initialization failed\n");
    return 1;
  }
  if (!options_.recordDir.empty()) return record();
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

    uploadPoints();
    render_.spin = sim_->runSpin();  // one spin for particles and the ray tracer
    const size_t count = sim_->backend().size();
    renderer_.render(makeFrameInput(fbWidth, fbHeight), render_);

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
    ui::UiContext ctx{*sim_, appearance(), render_, camera_, uiState_, renderer_, stats_, cudaAvailable_, usingCuda_};
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
