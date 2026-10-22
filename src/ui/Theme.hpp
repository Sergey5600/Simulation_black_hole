#pragma once

#include <imgui.h>

// Design tokens: the ONLY place where the UI's colors, radii, spacing and fonts
// are defined. A redesign should start here (see docs/UI.md).
namespace bh::ui {

struct ThemeTokens {
  // Surfaces
  ImVec4 panel{0.075f, 0.075f, 0.09f, 0.78f};          // floating glass panels
  ImVec4 panelBorder{1.0f, 1.0f, 1.0f, 0.07f};
  ImVec4 control{1.0f, 1.0f, 1.0f, 0.06f};             // slider tracks, inputs, segments
  ImVec4 controlHover{1.0f, 1.0f, 1.0f, 0.10f};
  ImVec4 controlActive{1.0f, 1.0f, 1.0f, 0.14f};
  ImVec4 separator{1.0f, 1.0f, 1.0f, 0.06f};

  // Accent (Apple system blue)
  ImVec4 accent{0.039f, 0.518f, 1.0f, 1.0f};
  ImVec4 accentHover{0.20f, 0.60f, 1.0f, 1.0f};
  ImVec4 accentActive{0.0f, 0.44f, 0.90f, 1.0f};

  // Text
  ImVec4 text{0.96f, 0.96f, 0.97f, 1.0f};
  ImVec4 textMuted{0.60f, 0.60f, 0.64f, 1.0f};
  ImVec4 textFaint{0.42f, 0.42f, 0.46f, 1.0f};

  // Status
  ImVec4 good{0.19f, 0.82f, 0.35f, 1.0f};
  ImVec4 warning{1.0f, 0.62f, 0.04f, 1.0f};

  // Geometry (logical pixels, multiplied by the DPI scale)
  float panelWidth = 352.0f;
  float panelMargin = 16.0f;
  float panelRounding = 16.0f;
  float panelPadding = 18.0f;
  float controlRounding = 8.0f;
  float itemSpacingY = 9.0f;
  float labelColumn = 0.40f;  // fraction of the row used by property labels

  // Typography (sizes in logical pixels)
  float fontSize = 15.0f;
  float titleFontSize = 21.0f;
  float captionFontSize = 12.5f;
};

struct Fonts {
  ImFont* body = nullptr;
  ImFont* semibold = nullptr;
  ImFont* title = nullptr;
  ImFont* caption = nullptr;
};

const ThemeTokens& tokens();
const Fonts& fonts();
float dpiScale();

// Loads fonts (first match of assets/fonts/*, then system fonts) and applies
// the tokens to ImGui's style. Call once after ImGui::CreateContext().
void applyTheme(float dpiScale);

}  // namespace bh::ui
