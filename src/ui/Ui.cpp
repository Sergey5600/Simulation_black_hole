#include "ui/Ui.hpp"

#include <cstdio>
#include <vector>

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

void drawSimulation(UiContext& ctx) {
  if (!BeginSection("Simulation")) return;
  SimControls& c = ctx.sim.controls();
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
  ToggleRow("Stars", &r.stars);
  EndSection();
}

void drawFooter(UiActions& out) {
  ImGui::Dummy(ImVec2(0.0f, 4.0f * dpiScale()));
  if (SecondaryButton("Save screenshot", ImVec2(-FLT_MIN, 0.0f))) out.screenshot = true;
  Caption("Drag: orbit  \xC2\xB7  Right-drag: pan  \xC2\xB7  Scroll: zoom\n"
          "Space: pause  \xC2\xB7  R: restart  \xC2\xB7  H: hide UI  \xC2\xB7  P: screenshot  \xC2\xB7  "
          "F: auto-rotate  \xC2\xB7  1/2: scenario");
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
    drawSimulation(ctx);
    drawScenarioParams(ctx, out);
    drawAppearance(ctx, out);
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

}  // namespace

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
