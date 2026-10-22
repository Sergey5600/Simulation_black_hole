#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/App.hpp"

#ifdef _WIN32
// Laptops with switchable graphics: ask the driver for the discrete NVIDIA/AMD
// GPU, so OpenGL and CUDA run on the same (fast) device.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {

void printUsage() {
  std::printf(
      "Usage: blackhole [options]\n"
      "  --scenario N        0 = accretion disk, 1 = light waves, 2 = star disruption\n"
      "  --size WxH          window size (default 1600x900)\n"
      "  --set Label=value   set a scenario parameter by its UI label, e.g. --set \"Particles=800000\"\n"
      "  --warmup T          pre-run T units of simulated time (M/c)\n"
      "  --view Y,P,D        camera yaw, pitch (radians) and distance (M)\n"
      "  --screenshot FILE   render a few frames, save a PNG and exit\n"
      "  --frames N          frames to render before --screenshot (default 4)\n"
      "  --hide-ui           start with the UI hidden\n"
      "  --no-vsync          uncapped frame rate\n"
      "  --backend B         auto (default: CUDA if available), cpu or cuda\n"
      "  --lensing           start with ray-traced gravitational lensing on\n"
      "  --spin A            black hole spin a/M for the ray tracer (-0.998 .. 0.998)\n");
}

}  // namespace

int main(int argc, char** argv) {
  bh::AppOptions opt;
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    auto next = [&]() -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "Missing value for %s\n", a);
        std::exit(2);
      }
      return argv[++i];
    };
    if (!std::strcmp(a, "--scenario")) {
      opt.scenario = std::atoi(next());
    } else if (!std::strcmp(a, "--size")) {
      std::sscanf(next(), "%dx%d", &opt.width, &opt.height);
    } else if (!std::strcmp(a, "--set")) {
      const std::string kv = next();
      const size_t eq = kv.find('=');
      if (eq != std::string::npos) opt.params.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
    } else if (!std::strcmp(a, "--warmup")) {
      opt.warmup = static_cast<float>(std::atof(next()));
    } else if (!std::strcmp(a, "--view")) {
      std::sscanf(next(), "%f,%f,%f", &opt.yaw, &opt.pitch, &opt.distance);
    } else if (!std::strcmp(a, "--screenshot")) {
      opt.screenshot = next();
    } else if (!std::strcmp(a, "--frames")) {
      opt.frames = std::atoi(next());
    } else if (!std::strcmp(a, "--hide-ui")) {
      opt.hideUi = true;
    } else if (!std::strcmp(a, "--lensing")) {
      opt.lensing = true;
    } else if (!std::strcmp(a, "--spin")) {
      opt.spin = static_cast<float>(std::atof(next()));
    } else if (!std::strcmp(a, "--backend")) {
      opt.backend = next();
    } else if (!std::strcmp(a, "--no-vsync")) {
      opt.vsync = false;
    } else {
      printUsage();
      return std::strcmp(a, "--help") && std::strcmp(a, "-h") ? 2 : 0;
    }
  }
  bh::App app(opt);
  return app.run();
}
