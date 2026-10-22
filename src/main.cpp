#include <algorithm>
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
      "  --spin A            black hole spin a/M for the ray tracer (-0.998 .. 0.998)\n"
      "  --ray-step S        ray-tracing step as a fraction of r (smaller = sharper, slower)\n"
      "  --lens-source S     what the ray tracer lenses: disk, particles or both\n"
      "  --classic-disk      disk brightness as bolometric T^4 g^4 (the old look) instead of the\n"
      "                      exact visible light of a black body\n"
      "\n"
      "Video recording (renders off-screen, resumable, then exits):\n"
      "  --record DIR        write frame_00000.jpg ... and make_video.bat into DIR\n"
      "  --record-size WxH   output resolution (default 3840x2160)\n"
      "  --duration S        seconds of video (default 60)\n"
      "  --fps N             frames per second (default 60)\n"
      "  --quality Q         preset: draft, film (default), max (9 jittered sub-frames with motion blur,\n"
      "                      2x finer ray steps; costs about as much as --ssaa 3)\n"
      "  --ssaa N            supersampling factor 1-4 (overrides the preset)\n"
      "  --samples N         jittered sub-frames per frame: anti-aliasing + motion blur\n"
      "  --shutter S         motion blur, fraction of the frame interval (0 = none, 0.5 = film look)\n"
      "  --path NAME         camera path: flyby (default), orbit, dive, or a first-person journey\n"
      "                      (--view Y,P,D sets its direction, inclination and start radius):\n"
      "                        infall  free fall from far away through the horizon\n"
      "                        hover   descend on engines to 2.01 M looking up (the sky shrinks into\n"
      "                                a bright circle, outside clocks run 14x faster), then fall\n"
      "  --hud               physical readout on journey frames (distance, clocks, tides, ...)\n"
      "  --auto-exposure B   on/off: meter the highlights like a video camera (default: on for\n"
      "                      journeys, whose sky gets 100x brighter near the horizon)\n"
      "  --mass M            hole mass in suns for the readout (default 4.3e6, Sagittarius A*)\n"
      "  --no-singularity    peach story without its artistic finale (the singularity layer)\n"
      "  --format F          jpg (default) or png\n"
      "  --layers LIST       also write separate sequences for compositing into DIR/<layer>/:\n"
      "                      beauty, sky, disk, particles, events, peach, singularity (linear light,\n"
      "                      alpha = coverage: they add up to beauty), hud (PNG with alpha), or all\n"
      "  --layer-format F    exr (default: half-float OpenEXR, linear) or png (tone-mapped)\n");
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
    } else if (!std::strcmp(a, "--lens-source")) {
      const std::string s = next();
      opt.lensSource = s == "particles" ? 1 : s == "both" ? 2 : 0;
    } else if (!std::strcmp(a, "--classic-disk")) {
      opt.classicDisk = true;
    } else if (!std::strcmp(a, "--ray-step")) {
      opt.rayStep = static_cast<float>(std::atof(next()));
    } else if (!std::strcmp(a, "--record")) {
      opt.recordDir = next();
    } else if (!std::strcmp(a, "--record-size")) {
      std::sscanf(next(), "%dx%d", &opt.recordWidth, &opt.recordHeight);
    } else if (!std::strcmp(a, "--duration")) {
      opt.recordSeconds = static_cast<float>(std::atof(next()));
    } else if (!std::strcmp(a, "--fps")) {
      opt.recordFps = std::max(1, std::atoi(next()));
    } else if (!std::strcmp(a, "--ssaa")) {
      opt.ssaa = std::atoi(next());
    } else if (!std::strcmp(a, "--quality")) {
      opt.quality = next();
    } else if (!std::strcmp(a, "--samples")) {
      opt.samples = std::atoi(next());
    } else if (!std::strcmp(a, "--shutter")) {
      opt.shutter = static_cast<float>(std::atof(next()));
    } else if (!std::strcmp(a, "--path")) {
      opt.cameraPath = next();
    } else if (!std::strcmp(a, "--auto-exposure")) {
      const std::string v = next();
      opt.autoExposure = v == "on" || v == "1" ? 1 : 0;
    } else if (!std::strcmp(a, "--no-singularity")) {
      opt.singularity = false;
    } else if (!std::strcmp(a, "--hud")) {
      opt.hud = true;
    } else if (!std::strcmp(a, "--mass")) {
      opt.solarMasses = std::max(1.0, std::atof(next()));
    } else if (!std::strcmp(a, "--layers")) {
      opt.layers = next();
    } else if (!std::strcmp(a, "--layer-format")) {
      opt.layerFormat = next();
    } else if (!std::strcmp(a, "--format")) {
      opt.frameFormat = next();
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
