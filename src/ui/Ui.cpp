#include "ui/Ui.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/Fall.hpp"
#include "ui/Theme.hpp"
#include "ui/Widgets.hpp"

namespace bh::ui {

namespace {

bool hasQuantity(Scenario& sc, Quantity q) {
  for (const QuantityOption& o : sc.quantities()) {
    if (o.quantity == q) return true;
  }
  return false;
}

void drawHeader(UiContext& ctx, UiActions& out) {
  ImGui::PushFont(fonts().title);
  ImGui::TextUnformatted("Black Hole");
  ImGui::PopFont();
  Caption("Schwarzschild metric  \xC2\xB7  G = c = M = 1");
  ImGui::Dummy(ImVec2(0.0f, 2.0f * dpiScale()));

  std::vector<const char*> names;
  for (size_t i = 0; i < ctx.sim.scenarioCount(); ++i) names.push_back(ctx.sim.scenario(i).name());
  int current = static_cast<int>(ctx.sim.currentIndex());
  if (SegmentedControl("scenario", &current, names)) out.selectScenario = current;
  Caption(ctx.sim.current().subtitle());
}

void drawTransport(UiContext& ctx, UiActions& out) {
  SimControls& c = ctx.sim.controls();
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float w = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
  if (PrimaryButton(c.paused ? "Play" : "Pause", ImVec2(w, 0.0f))) c.paused = !c.paused;
  ImGui::SameLine();
  if (SecondaryButton("Restart", ImVec2(w, 0.0f))) out.reset = true;
}

void drawSimulation(UiContext& ctx, UiActions& out) {
  if (!BeginSection("Simulation")) return;
  SimControls& c = ctx.sim.controls();

  PropertyLabel("Compute", "Where the physics runs. CUDA keeps every particle on the GPU and writes vertices "
                           "straight into the OpenGL buffer.");
  if (ctx.cudaAvailable) {
    static const char* const kBackends[] = {"CPU", "GPU \xC2\xB7 CUDA"};
    int backend = ctx.usingCuda ? 1 : 0;
    if (SegmentedControl("backend", &backend, kBackends)) out.selectBackend = backend;
  } else {
    ImGui::BeginDisabled();
    static const char* const kCpuOnly[] = {"CPU"};
    int cpu = 0;
    SegmentedControl("backend", &cpu, kCpuOnly);
    ImGui::EndDisabled();
  }

  SliderRow("Speed", &c.timeScale, 0.5f, 500.0f, "%.1f M/s", true,
            "Simulated time per real second, in units of M/c (GM/c\xC2\xB3). For a 10 solar-mass black hole "
            "M/c is 49 microseconds; for Sagittarius A* it is about 20 seconds.");

  PropertyLabel("Clock",
                "Observer t: coordinate time of a distant observer. Infalling gas slows down, dims and freezes "
                "at the horizon, exactly as a telescope would see it.\n\nProper \xCF\x84: every particle's own "
                "clock. Gas crosses the horizon in finite time.");
  static const char* const kClocks[] = {"Observer t", "Proper \xCF\x84"};
  int mode = static_cast<int>(c.timeMode);
  if (SegmentedControl("clock", &mode, kClocks)) c.timeMode = static_cast<TimeMode>(mode);

  SliderRow("Step size", &c.accuracy, 0.002f, 0.1f, "%.3f", true,
            "RK4 step as a fraction of the local dynamical time. Smaller is more accurate and slower.");
  SliderRow("Spin a", &c.spin, -0.998f, 0.998f, "%.3f", false,
            "Rotation of the hole (Kerr metric), a = J/(Mc). Positive = same sense as the disk. Particles and "
            "the ray tracer both use it: frame dragging, a lopsided shadow, and an ISCO that moves in (1.24 M "
            "at a = 0.998) or out (9 M at a = -1). Restarts the scenario.");
  if (ImGui::IsItemDeactivatedAfterEdit()) out.reset = true;
  EndSection();
}

void drawScenarioParams(UiContext& ctx, UiActions& out) {
  Scenario& sc = ctx.sim.current();
  if (!BeginSection(sc.name())) return;
  for (ParamDesc& d : sc.parameters()) {
    ImGui::PushID(d.label);
    bool changed = false;
    bool commit = false;  // reset only once the user lets go of a count slider
    switch (d.type) {
      case ParamDesc::Type::Float:
        changed = SliderRow(d.label, static_cast<float*>(d.value), d.min, d.max, d.format ? d.format : "%.2f",
                            d.logarithmic, d.help);
        commit = changed;
        break;
      case ParamDesc::Type::Int:
        changed = SliderIntRow(d.label, static_cast<int*>(d.value), static_cast<int>(d.min), static_cast<int>(d.max),
                               d.logarithmic, d.help);
        commit = ImGui::IsItemDeactivatedAfterEdit();
        break;
      case ParamDesc::Type::Bool:
        changed = commit = ToggleRow(d.label, static_cast<bool*>(d.value), d.help);
        break;
      case ParamDesc::Type::Enum:
        changed = commit = ComboRow(d.label, static_cast<int*>(d.value), d.options, d.help);
        break;
    }
    (void)changed;  // live parameters take effect on the next spawn/step
    if (commit && d.requiresReset) out.reset = true;
    ImGui::PopID();
  }
  EndSection();
}

void drawAppearance(UiContext& ctx, UiActions& out) {
  if (!BeginSection("Appearance")) return;
  Appearance& a = ctx.appearance;
  Scenario& sc = ctx.sim.current();
  const std::vector<QuantityOption> quantities = sc.quantities();
  const bool canBlackbody = hasQuantity(sc, Quantity::Temperature);
  const bool photons = hasQuantity(sc, Quantity::Age);

  if (canBlackbody) {
    static const char* const kStyles[] = {"Physical", "Data"};
    int style = static_cast<int>(a.style);
    if (SegmentedControl("style", &style, kStyles)) a.style = static_cast<ColorStyle>(style);
  }

  if (canBlackbody && a.style == ColorStyle::Blackbody) {
    SliderRow("Peak temperature", &a.peakTemperature, 1500.0f, 40000.0f, "%.0f K", true,
              "Black-body temperature of the hottest ring (r = 8.2 M). Real disks are far hotter: ~10^7 K for "
              "stellar black holes (X-rays) and ~10^5 K for supermassive ones, which looks blue-white. "
              "Interstellar used a warm ~4500 K palette for the eye.");
    ToggleRow("Doppler beaming", &a.dopplerBeaming,
              "Brightness scales as g^4: gas moving towards you is much brighter.");
    ToggleRow("Color shift", &a.colorShift,
              "Observed temperature = g \xC2\xB7 T: approaching gas looks bluer, receding and deep gas redder.");
  } else {
    int qIndex = 0;
    std::vector<const char*> labels;
    for (size_t i = 0; i < quantities.size(); ++i) {
      labels.push_back(quantities[i].label);
      if (quantities[i].quantity == a.quantity) qIndex = static_cast<int>(i);
    }
    if (ComboRow("Color by", &qIndex, labels)) {
      const QuantityOption& q = quantities[static_cast<size_t>(qIndex)];
      a.quantity = q.quantity;
      a.rangeMin = q.defaultMin;
      a.rangeMax = q.defaultMax;
      a.style = ColorStyle::Colormap;
    }
    int cmap = static_cast<int>(a.colormap);
    if (ComboRow("Color map", &cmap, kColormapNames)) a.colormap = static_cast<Colormap>(cmap);

    char lo[32], hi[32];
    std::snprintf(lo, sizeof(lo), "%.3g", a.rangeMin);
    std::snprintf(hi, sizeof(hi), "%.3g", a.rangeMax);
    ColormapBar(static_cast<ImTextureID>(ctx.renderer.colormapTexture(a.colormap)), lo, hi);

    PropertyLabel("Range", "Values mapped to the two ends of the color map. Swap them to reverse it.");
    float range[2] = {a.rangeMin, a.rangeMax};
    if (ImGui::DragFloat2("##range", range, 0.01f)) {
      a.rangeMin = range[0];
      a.rangeMax = range[1];
    }
    if (!photons) ToggleRow("Doppler beaming", &a.dopplerBeaming, "Brightness scales as g^4.");
  }

  SliderRow("Point size", &a.pointSize, 0.01f, 1.0f, "%.3f M", true, "Sprite diameter in units of M.");
  SliderRow("Brightness", &a.brightness, 0.05f, 20.0f, "%.2f", true);
  if (photons) ToggleRow("Fade old fronts", &a.fadeWithAge);

  if (SecondaryButton("Reset look", ImVec2(-FLT_MIN, 0.0f))) out.resetAppearance = true;
  EndSection();
}

void drawRayTracing(UiContext& ctx) {
  if (!BeginSection("Ray tracing")) return;
  RenderSettings& r = ctx.render;
  ToggleRow("Gravitational lensing", &r.lensing,
            "Trace a light ray backwards from the camera for every pixel along the exact Schwarzschild "
            "geodesic: the sky bends, the shadow appears (radius 3\xE2\x88\x9A" "3 M) and the back of the disk "
            "shows above and below the hole. Key: L.");
  if (r.lensing) {
    Appearance& a = ctx.appearance;
    PropertyLabel("Light from",
                  "What the ray tracer sees. Thin disk: analytic Novikov-Thorne disk with the exact redshift of "
                  "every ray. Particles: the simulated particles, splatted into a 3D grid and lensed (multiple "
                  "images, Einstein rings). Both: both at once.");
    static const char* const kSources[] = {"Disk", "Particles", "Both"};
    SegmentedControl("lensSource", &a.lensSource, kSources);
    if (a.lensSource != Appearance::Particles) {
      ToggleRow("Inner edge at ISCO", &r.diskAtIsco,
                "A thin disk ends at the innermost stable circular orbit (6 M without spin).");
      if (!r.diskAtIsco) SliderRow("Disk inner edge", &r.diskInner, 1.0f, 20.0f, "%.1f M");
      SliderRow("Disk outer edge", &r.diskOuter, 8.0f, 120.0f, "%.0f M");
      SliderRow("Disk temperature", &r.diskTemperature, 1500.0f, 40000.0f, "%.0f K", true);
      SliderRow("Disk brightness", &r.diskBrightness, 0.05f, 20.0f, "%.2f", true);
      ToggleRow("Auto exposure", &r.diskAutoExposure,
                "With spin the inner disk gets much hotter and brighter (up to 32% of the rest mass is radiated "
                "instead of 6%). This partially compensates so the image does not blow out; colors stay physical.");
      ToggleRow("Visible spectrum", &r.diskVisibleSpectrum,
                "On: what a camera sees. A black body seen with shift g is a black body at g \xC2\xB7 T, and "
                "its visible light changes much less than its total power (most of it moves to UV or IR). "
                "Off: classic look, brightness follows the bolometric T\xE2\x81\xB4 g\xE2\x81\xB4.");
      ToggleRow("Disk beaming", &r.diskBeaming, "Brightness changes with the shift g (Doppler + gravity).");
      ToggleRow("Disk color shift", &r.diskColorShift, "Observed temperature = g \xC2\xB7 T.");
    }
    if (a.lensSource != Appearance::ThinDisk) {
      SliderRow("Particle glow", &r.volumeGlow, 0.05f, 20.0f, "%.2f", true, "Brightness of the lensed particles.");
      SliderRow("Volume size", &a.volumeExtent, 10.0f, 300.0f, "%.0f M", true,
                "Edge of the cube around the hole that holds the lensed particles. Particles outside it are not "
                "drawn; a smaller cube gives finer detail.");
      SliderIntRow("Volume detail", &r.volumeResolution, 64, 384, false,
                   "Voxels per edge. 256 = 16.7 M voxels, ~330 MB of video memory.");
    } else {
      ToggleRow("Show particles", &r.particlesWhenLensing,
                "Overlay the simulated particles without lensing (they will not line up with the bent image). "
                "Choose \"Particles\" above to lens them.");
    }
    SliderRow("Ray step", &r.rayStepScale, 0.005f, 0.1f, "%.3f", true,
              "Integration step as a fraction of the distance to the hole. Smaller = sharper photon ring, "
              "slower.");
    SliderIntRow("Max steps", &r.rayMaxSteps, 100, 3000, true);
  }
  SliderRow("Sky brightness", &r.skyBrightness, 0.0f, 5.0f, "%.2f");
  EndSection();
}

void drawPost(UiContext& ctx) {
  if (!BeginSection("Image", false)) return;
  RenderSettings& r = ctx.render;
  SliderRow("Exposure", &r.exposure, 0.05f, 20.0f, "%.2f", true);
  ToggleRow("Bloom", &r.bloom);
  if (r.bloom) {
    SliderRow("Bloom strength", &r.bloomStrength, 0.0f, 0.4f, "%.3f");
    SliderRow("Bloom radius", &r.bloomRadius, 0.5f, 3.0f, "%.2f");
  }
  SliderRow("Vignette", &r.vignette, 0.0f, 1.0f, "%.2f");
  EndSection();
}

void drawCamera(UiContext& ctx, UiActions& out) {
  if (!BeginSection("Camera", false)) return;
  OrbitCamera& cam = ctx.camera;
  SliderRow("Field of view", &cam.fovDegrees, 15.0f, 90.0f, "%.0f\xC2\xB0");
  ToggleRow("Auto-rotate", &cam.autoRotate);
  if (cam.autoRotate) SliderRow("Rotation speed", &cam.autoRotateSpeed, -0.5f, 0.5f, "%.2f rad/s");
  if (SecondaryButton("Reset view", ImVec2(-FLT_MIN, 0.0f))) out.resetView = true;
  EndSection();
}

void drawGuides(UiContext& ctx) {
  if (!BeginSection("Guides", false)) return;
  RenderSettings& r = ctx.render;
  ToggleRow("Horizon  r = 2M", &r.guideHorizon);
  ToggleRow("Photon sphere  3M", &r.guidePhotonSphere, "Light can orbit here (unstably).");
  ToggleRow("ISCO  6M", &r.guideIsco, "Innermost stable circular orbit: gas inside it plunges.");
  ToggleRow("Grid", &r.guideGrid, "Rings every 10 M in the equatorial plane.");
  ToggleRow("Sky", &r.stars);
  EndSection();
}

void drawFooter(UiActions& out) {
  ImGui::Dummy(ImVec2(0.0f, 4.0f * dpiScale()));
  if (SecondaryButton("Save screenshot", ImVec2(-FLT_MIN, 0.0f))) out.screenshot = true;
  Caption("Drag: orbit  \xC2\xB7  Right-drag: pan  \xC2\xB7  Scroll: zoom\n"
          "Space: pause  \xC2\xB7  R: restart  \xC2\xB7  H: hide UI  \xC2\xB7  P: screenshot  \xC2\xB7  "
          "F: auto-rotate  \xC2\xB7  L: lensing  \xC2\xB7  1-3: scenario");
}

void drawPanel(UiContext& ctx, UiActions& out) {
  const ThemeTokens& tk = tokens();
  const float s = dpiScale();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float width = tk.panelWidth * s;
  const float margin = tk.panelMargin * s;
  ImGui::SetNextWindowPos(ImVec2(margin, margin));
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, display.y - 2.0f * margin));

  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoCollapse;
  if (ImGui::Begin("Controls", nullptr, flags)) {
    drawHeader(ctx, out);
    ImGui::Dummy(ImVec2(0.0f, 2.0f * s));
    drawTransport(ctx, out);
    drawSimulation(ctx, out);
    drawScenarioParams(ctx, out);
    drawAppearance(ctx, out);
    drawRayTracing(ctx);
    drawPost(ctx);
    drawCamera(ctx, out);
    drawGuides(ctx);
    drawFooter(out);
  }
  ImGui::End();
}

void drawHud(UiContext& ctx) {
  const ThemeTokens& tk = tokens();
  const float s = dpiScale();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float margin = tk.panelMargin * s;
  ImGui::SetNextWindowPos(ImVec2(display.x - margin, display.y - margin), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
  ImGui::SetNextWindowBgAlpha(0.55f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * s, 10.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 3.0f * s));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
  if (ImGui::Begin("##hud", nullptr, flags)) {
    const FrameStats& st = ctx.stats;
    ImGui::PushFont(fonts().semibold);
    ImGui::Text("%.0f FPS", st.fps);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, tk.textMuted);
    ImGui::Text("%.1f ms frame  \xC2\xB7  %.1f ms physics", st.frameMs, st.simMs);
    ImGui::Text("%zu / %zu particles", st.alive, st.total);
    ImGui::Text("t = %.0f M/c  \xC2\xB7  %s%s", st.simTime, ctx.sim.controls().timeMode == TimeMode::Coordinate ? "observer" : "proper",
                ctx.sim.controls().paused ? "  \xC2\xB7  paused" : "");
    ImGui::PushStyleColor(ImGuiCol_Text, tk.textFaint);
    ImGui::TextUnformatted(st.backend);
    ImGui::PopStyleColor(2);
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
}

void drawHiddenHint() {
  const float s = dpiScale();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(tokens().panelMargin * s, display.y - tokens().panelMargin * s), ImGuiCond_Always,
                          ImVec2(0.0f, 1.0f));
  ImGui::SetNextWindowBgAlpha(0.0f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoNav;
  if (ImGui::Begin("##hint", nullptr, flags)) {
    ImGui::PushFont(fonts().caption);
    ImGui::PushStyleColor(ImGuiCol_Text, tokens().textFaint);
    ImGui::TextUnformatted("H  show controls");
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  ImGui::End();
}

std::string formatDistance(double meters) {
  char b[64];
  if (meters >= 1e9) std::snprintf(b, sizeof(b), "%.1f million km", meters / 1e9);
  else if (meters >= 1e4) std::snprintf(b, sizeof(b), "%.0f km", meters / 1e3);
  else std::snprintf(b, sizeof(b), "%.0f m", meters);
  return b;
}

std::string formatDuration(double seconds) {
  char b[64];
  const double year = 3.15576e7;
  if (seconds < 1e-3) std::snprintf(b, sizeof(b), "%.0f \xC2\xB5s", seconds * 1e6);
  else if (seconds < 1.0) std::snprintf(b, sizeof(b), "%.0f ms", seconds * 1e3);
  else if (seconds < 120.0) std::snprintf(b, sizeof(b), "%.1f s", seconds);
  else if (seconds < 7200.0) std::snprintf(b, sizeof(b), "%.1f min", seconds / 60.0);
  else if (seconds < 172800.0) std::snprintf(b, sizeof(b), "%.1f h", seconds / 3600.0);
  else if (seconds < year) std::snprintf(b, sizeof(b), "%.1f days", seconds / 86400.0);
  else std::snprintf(b, sizeof(b), "%.3g years", seconds / year);
  return b;
}

std::string formatAcceleration(double mps2) {
  char b[64];
  const double g = mps2 / 9.80665;
  if (g >= 1e6) std::snprintf(b, sizeof(b), "%.1f million g", g / 1e6);
  else if (g >= 10.0) std::snprintf(b, sizeof(b), "%.0f g", g);
  else std::snprintf(b, sizeof(b), "%.2f g", g);
  return b;
}

}  // namespace

void drawJourneyHud(const JourneyFrame& f, double solarMasses, float exposureEv) {
  const ThemeTokens& tk = tokens();
  const float s = dpiScale();
  const fall::Units units(solarMasses);
  const double r = f.r;
  const bool inside = r < 2.0;
  const double energy = f.hovering ? fall::energyFromRest(r) : f.energy;
  const double clock = f.hovering ? fall::overheadShiftStatic(r) : fall::overheadShiftFalling(r, energy);
  char line[160];

  ImGui::SetNextWindowPos(ImVec2(tk.panelMargin * 2.0f * s, ImGui::GetIO().DisplaySize.y - tk.panelMargin * 2.0f * s),
                          ImGuiCond_Always, ImVec2(0.0f, 1.0f));
  ImGui::SetNextWindowBgAlpha(0.42f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * s, 12.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 4.0f * s));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
  if (ImGui::Begin("##journey", nullptr, flags)) {
    const float valueColumn = 118.0f * s;
    auto row = [&](const char* label, const std::string& value) {
      ImGui::PushStyleColor(ImGuiCol_Text, tk.textMuted);
      ImGui::TextUnformatted(label);
      ImGui::PopStyleColor();
      ImGui::SameLine(valueColumn);
      ImGui::TextUnformatted(value.c_str());
    };
    ImGui::PushFont(fonts().semibold);
    std::snprintf(line, sizeof(line), "r = %.3f M", r);
    ImGui::TextUnformatted(line);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, tk.textMuted);
    ImGui::TextUnformatted(("  " + formatDistance(r * units.length) + " from the center").c_str());
    ImGui::PopStyleColor();

    if (inside) row("horizon", "crossed, " + formatDistance((2.0 - r) * units.length) + " inside");
    else row("horizon", formatDistance((r - 2.0) * units.length) + " below");
    if (f.hovering) {
      row("engines", "hovering at " + formatAcceleration(fall::hoverAcceleration(r) * units.accel));
    } else if (!inside) {
      std::snprintf(line, sizeof(line), "off, falling at %.3f c", fall::localSpeed(r, energy));
      row("engines", line);
    } else {
      row("engines", "off: inside, every path leads to r = 0");
    }
    std::snprintf(line, sizeof(line), "\xC3\x97%.2f  (light from overhead, %s)", clock,
                  clock >= 1.0 ? "blueshifted" : "redshifted");
    row("outside clocks", line);
    row("outside time", "+" + formatDuration(f.outsideTime * units.time) + " since the start");
    row("on board", "+" + formatDuration(f.properTime * units.time));
    std::snprintf(line, sizeof(line), "%.3g m/s\xC2\xB2 per meter", fall::tidalStretch(r) * units.accel / units.length);
    row("tidal stretch", line);
    const double left = fall::properTimeToSingularity(r, energy) * units.time;
    row("singularity", f.hovering ? formatDuration(left) + " away if the engines stop" : "in " + formatDuration(left));

    if (f.cosmicTime >= 0.0f) {
      std::snprintf(line, sizeof(line), "+%.0f billion years  (story time-lapse, not physics)", 100.0 * f.cosmicTime);
      row("universe", line);
    }
    if (f.peach) {
      if (f.tornApart) {
        std::snprintf(line, sizeof(line), "torn by tides: stretched \xC3\x97%.1f, squeezed \xC3\x97%.2f", f.peachStretch,
                      f.peachSqueeze);
        row("peach", line);
      } else {
        const double tides = fall::tidalStretch(r) * units.accel / units.length * 0.076 * 0.076 * 1000.0 / 4.0;
        std::snprintf(line, sizeof(line), "intact, tidal stress %.3g Pa of ~100 kPa", tides);
        row("peach", line);
      }
    }
    if (f.singularity > 0.0f) row("finale", "artistic: general relativity has no answer at r = 0");
    if (!std::isnan(exposureEv)) {
      std::snprintf(line, sizeof(line), "%+.1f EV (auto, like a video camera)", exposureEv);
      row("exposure", line);
    }

    ImGui::PushFont(fonts().caption);
    ImGui::PushStyleColor(ImGuiCol_Text, tk.textFaint);
    if (solarMasses >= 1e6) std::snprintf(line, sizeof(line), "Schwarzschild hole of %.3g million suns", solarMasses / 1e6);
    else std::snprintf(line, sizeof(line), "Schwarzschild hole of %.0f suns", solarMasses);
    ImGui::TextUnformatted(line);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
}

UiActions drawUi(UiContext& ctx) {
  UiActions out;
  if (ctx.state.showPanel) {
    drawPanel(ctx, out);
    if (ctx.state.showHud) drawHud(ctx);
  } else {
    drawHiddenHint();
  }
  return out;
}

}  // namespace bh::ui
