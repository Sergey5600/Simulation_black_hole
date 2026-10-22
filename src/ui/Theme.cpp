#include "ui/Theme.hpp"

#include <filesystem>
#include <initializer_list>

namespace bh::ui {

namespace {

ThemeTokens g_tokens;
Fonts g_fonts;
float g_scale = 1.0f;

// Latin, Latin-1, Greek, Cyrillic, punctuation, super/subscripts, arrows, math.
const ImWchar kGlyphRanges[] = {0x0020, 0x00FF, 0x0370, 0x03FF, 0x0400, 0x04FF, 0x2010, 0x205E,
                                0x2070, 0x209F, 0x2190, 0x21FF, 0x2200, 0x22FF, 0};

const char* firstExisting(std::initializer_list<const char*> candidates) {
  for (const char* path : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) return path;
  }
  return nullptr;
}

ImFont* loadFont(const char* path, float size, const char* symbolsPath) {
  ImGuiIO& io = ImGui::GetIO();
  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 2;
  ImFont* font = path ? io.Fonts->AddFontFromFileTTF(path, size, &cfg, kGlyphRanges) : nullptr;
  if (!font) {
    ImFontConfig fallback;
    fallback.SizePixels = size;
    font = io.Fonts->AddFontDefault(&fallback);
  }
  if (symbolsPath) {  // fill in math symbols missing from the main font
    ImFontConfig merge;
    merge.MergeMode = true;
    merge.OversampleH = 2;
    static const ImWchar kSymbols[] = {0x2190, 0x21FF, 0x2200, 0x22FF, 0};
    io.Fonts->AddFontFromFileTTF(symbolsPath, size, &merge, kSymbols);
  }
  return font;
}

void loadFonts(float scale) {
  // Drop Inter (or any TTF) into assets/fonts/ to override the system font.
  const char* regular = firstExisting({
      "assets/fonts/Inter-Regular.ttf", "../assets/fonts/Inter-Regular.ttf",
      "C:/Windows/Fonts/segoeui.ttf",
      "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  });
  const char* semibold = firstExisting({
      "assets/fonts/Inter-SemiBold.ttf", "../assets/fonts/Inter-SemiBold.ttf",
      "C:/Windows/Fonts/seguisb.ttf",
      "/usr/share/fonts/truetype/inter/Inter-SemiBold.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
  });
  if (!semibold) semibold = regular;
  const char* symbols = firstExisting({"C:/Windows/Fonts/seguisym.ttf"});

  const ThemeTokens& t = g_tokens;
  ImGui::GetIO().Fonts->Clear();
  g_fonts.body = loadFont(regular, t.fontSize * scale, symbols);
  g_fonts.semibold = loadFont(semibold, t.fontSize * scale, symbols);
  g_fonts.title = loadFont(semibold, t.titleFontSize * scale, symbols);
  g_fonts.caption = loadFont(regular, t.captionFontSize * scale, symbols);
  ImGui::GetIO().FontDefault = g_fonts.body;
}

}  // namespace

const ThemeTokens& tokens() { return g_tokens; }
const Fonts& fonts() { return g_fonts; }
float dpiScale() { return g_scale; }

void applyTheme(float scale) {
  g_scale = scale;
  loadFonts(scale);

  const ThemeTokens& t = g_tokens;
  ImGuiStyle& s = ImGui::GetStyle();
  s = ImGuiStyle();

  s.WindowPadding = ImVec2(t.panelPadding, t.panelPadding);
  s.WindowRounding = t.panelRounding;
  s.WindowBorderSize = 1.0f;
  s.ChildRounding = t.controlRounding;
  s.PopupRounding = t.controlRounding;
  s.PopupBorderSize = 1.0f;
  s.FramePadding = ImVec2(10.0f, 6.0f);
  s.FrameRounding = t.controlRounding;
  s.FrameBorderSize = 0.0f;
  s.ItemSpacing = ImVec2(8.0f, t.itemSpacingY);
  s.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
  s.IndentSpacing = 14.0f;
  s.ScrollbarSize = 10.0f;
  s.ScrollbarRounding = 8.0f;
  s.GrabMinSize = 14.0f;
  s.GrabRounding = t.controlRounding;
  s.TabRounding = t.controlRounding;
  s.SeparatorTextBorderSize = 1.0f;
  s.WindowTitleAlign = ImVec2(0.0f, 0.5f);

  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = t.text;
  c[ImGuiCol_TextDisabled] = t.textFaint;
  c[ImGuiCol_WindowBg] = t.panel;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.11f, 0.13f, 0.97f);
  c[ImGuiCol_Border] = t.panelBorder;
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = t.control;
  c[ImGuiCol_FrameBgHovered] = t.controlHover;
  c[ImGuiCol_FrameBgActive] = t.controlActive;
  c[ImGuiCol_TitleBg] = t.panel;
  c[ImGuiCol_TitleBgActive] = t.panel;
  c[ImGuiCol_TitleBgCollapsed] = t.panel;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = t.controlActive;
  c[ImGuiCol_ScrollbarGrabHovered] = t.controlActive;
  c[ImGuiCol_ScrollbarGrabActive] = t.accent;
  c[ImGuiCol_CheckMark] = t.accent;
  c[ImGuiCol_SliderGrab] = t.accent;
  c[ImGuiCol_SliderGrabActive] = t.accentActive;
  c[ImGuiCol_Button] = t.control;
  c[ImGuiCol_ButtonHovered] = t.controlHover;
  c[ImGuiCol_ButtonActive] = t.controlActive;
  c[ImGuiCol_Header] = t.control;
  c[ImGuiCol_HeaderHovered] = t.controlHover;
  c[ImGuiCol_HeaderActive] = t.controlActive;
  c[ImGuiCol_Separator] = t.separator;
  c[ImGuiCol_SeparatorHovered] = t.separator;
  c[ImGuiCol_SeparatorActive] = t.accent;
  c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ResizeGripHovered] = t.controlHover;
  c[ImGuiCol_ResizeGripActive] = t.accent;
  c[ImGuiCol_PlotLines] = t.accent;
  c[ImGuiCol_PlotHistogram] = t.accent;
  c[ImGuiCol_TextSelectedBg] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.35f);
  c[ImGuiCol_NavCursor] = t.accent;
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);

  s.ScaleAllSizes(scale);
}

}  // namespace bh::ui
