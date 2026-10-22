#include "ui/Widgets.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include "ui/Theme.hpp"

namespace bh::ui {

namespace {

ImU32 col(const ImVec4& c, float alphaScale = 1.0f) {
  return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alphaScale));
}

// Animates a 0..1 value per widget id (Apple-style eased transitions).
float animate(ImGuiID id, float target, float speed = 14.0f) {
  ImGuiStorage* storage = ImGui::GetStateStorage();
  float* v = storage->GetFloatRef(id, target);
  *v += (target - *v) * std::min(1.0f, ImGui::GetIO().DeltaTime * speed);
  return *v;
}

// Position of v on the slider track (0..1), matching ImGui's log mapping closely enough for a fill.
float sliderFraction(float v, float min, float max, bool log) {
  if (max == min) return 0.0f;
  if (log && min > 0.0f && max > 0.0f) {
    return ImClamp(std::log(std::max(v, min) / min) / std::log(max / min), 0.0f, 1.0f);
  }
  return ImClamp((v - min) / (max - min), 0.0f, 1.0f);
}

// Control-Center style slider: the track fills with the accent color up to the
// value and the number stays readable on top. Wraps a regular ImGui slider so
// keyboard input (Ctrl+click) keeps working.
template <typename Fn>
bool FillSlider(float fraction, Fn&& slider) {
  const ThemeTokens& tk = tokens();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->ChannelsSplit(2);
  dl->ChannelsSetCurrent(1);
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 1.0f);
  const bool changed = slider();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);

  const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
  const bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
  const float t = animate(ImGui::GetItemID(), fraction, 24.0f);
  dl->ChannelsSetCurrent(0);
  const float rounding = tk.controlRounding * dpiScale();
  const float x = min.x + (max.x - min.x) * t;
  if (x > min.x + 0.5f) {
    const ImVec4 fill = active ? tk.accentActive : (hovered ? tk.accentHover : tk.accent);
    dl->AddRectFilled(min, ImVec2(std::max(x, min.x + rounding * 2.0f), max.y), col(fill, 0.85f), rounding);
  }
  dl->ChannelsMerge();
  return changed;
}

}  // namespace

bool ToggleSwitch(const char* id, bool* value) {
  const float s = dpiScale();
  const float height = ImGui::GetFrameHeight() * 0.86f;
  const float width = height * 1.75f;
  const float avail = ImGui::GetContentRegionAvail().x;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, avail - width));
  // Vertically center against regular frame height.
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::GetFrameHeight() - height) * 0.5f);

  const ImVec2 p = ImGui::GetCursorScreenPos();
  bool changed = false;
  if (ImGui::InvisibleButton(id, ImVec2(width, height))) {
    *value = !*value;
    changed = true;
  }
  const float t = animate(ImGui::GetItemID(), *value ? 1.0f : 0.0f);

  const ThemeTokens& tk = tokens();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec4 off = ImGui::IsItemHovered() ? tk.controlActive : tk.controlHover;
  const ImVec4 bg = ImLerp(off, tk.accent, t);
  dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col(bg), height * 0.5f);
  const float r = height * 0.5f - 2.0f * s;
  const ImVec2 knob(p.x + height * 0.5f + t * (width - height), p.y + height * 0.5f);
  dl->AddCircleFilled(ImVec2(knob.x, knob.y + 1.0f * s), r, IM_COL32(0, 0, 0, 60));
  dl->AddCircleFilled(knob, r, IM_COL32(255, 255, 255, 255));
  return changed;
}

bool SegmentedControl(const char* id, int* current, std::span<const char* const> labels) {
  const ThemeTokens& tk = tokens();
  const float s = dpiScale();
  ImGui::PushID(id);
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = ImGui::GetFrameHeight() + 2.0f * s;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col(tk.control), tk.controlRounding * s);

  const int n = static_cast<int>(labels.size());
  const float segment = width / static_cast<float>(std::max(n, 1));
  const float pad = 2.0f * s;

  // Sliding selection pill.
  const float x = animate(ImGui::GetID("pill"), static_cast<float>(*current), 18.0f);
  const ImVec2 a(p.x + x * segment + pad, p.y + pad);
  dl->AddRectFilled(a, ImVec2(a.x + segment - 2.0f * pad, p.y + height - pad), col(tk.controlActive, 1.6f),
                    (tk.controlRounding - 1.0f) * s);

  bool changed = false;
  for (int i = 0; i < n; ++i) {
    ImGui::SetCursorScreenPos(ImVec2(p.x + static_cast<float>(i) * segment, p.y));
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("seg", ImVec2(segment, height)) && *current != i) {
      *current = i;
      changed = true;
    }
    const bool selected = *current == i;
    const char* label = labels[static_cast<size_t>(i)];
    ImGui::PushFont(selected ? fonts().semibold : fonts().body);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const ImVec2 tp(p.x + static_cast<float>(i) * segment + (segment - ts.x) * 0.5f, p.y + (height - ts.y) * 0.5f);
    dl->AddText(tp, col(selected || ImGui::IsItemHovered() ? tk.text : tk.textMuted), label);
    ImGui::PopFont();
    ImGui::PopID();
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + height));
  ImGui::Dummy(ImVec2(width, 0.0f));
  ImGui::PopID();
  return changed;
}

bool BeginSection(const char* title, bool defaultOpen) {
  const ThemeTokens& tk = tokens();
  ImGui::Dummy(ImVec2(0.0f, 4.0f * dpiScale()));
  std::string upper(title);
  std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });

  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, tk.textMuted);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 4.0f * dpiScale()));
  ImGui::PushFont(fonts().caption);
  const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                   (defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
  ImGui::PushID(title);
  const bool open = ImGui::TreeNodeEx("##section", flags, "%s", upper.c_str());
  ImGui::PopID();
  ImGui::PopFont();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);

  // Hairline under the header.
  const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), col(tk.separator));
  if (open) ImGui::Dummy(ImVec2(0.0f, 2.0f * dpiScale()));
  return open;
}

void EndSection() {}

void PropertyLabel(const char* label, const char* help) {
  const ThemeTokens& tk = tokens();
  const float full = ImGui::GetContentRegionAvail().x;
  const float column = full * tk.labelColumn;
  ImGui::AlignTextToFramePadding();
  ImGui::PushStyleColor(ImGuiCol_Text, tk.textMuted);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  if (help && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetNextWindowSize(ImVec2(280.0f * dpiScale(), 0.0f));
    ImGui::BeginTooltip();
    ImGui::TextWrapped("%s", help);
    ImGui::EndTooltip();
  }
  ImGui::SameLine(ImGui::GetCursorStartPos().x + column);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

bool SliderRow(const char* label, float* v, float min, float max, const char* format, bool log, const char* help) {
  ImGui::PushID(label);
  PropertyLabel(label, help);
  const bool changed = FillSlider(sliderFraction(*v, min, max, log), [&] {
    return ImGui::SliderFloat("##v", v, min, max, format, log ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
  });
  ImGui::PopID();
  return changed;
}

bool SliderIntRow(const char* label, int* v, int min, int max, bool log, const char* help) {
  ImGui::PushID(label);
  PropertyLabel(label, help);
  const float fraction =
      sliderFraction(static_cast<float>(*v), static_cast<float>(min), static_cast<float>(max), log);
  const bool changed = FillSlider(fraction, [&] {
    return ImGui::SliderInt("##v", v, min, max, "%d", log ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
  });
  ImGui::PopID();
  return changed;
}

bool ToggleRow(const char* label, bool* v, const char* help) {
  ImGui::PushID(label);
  PropertyLabel(label, help);
  const bool changed = ToggleSwitch("##toggle", v);
  ImGui::PopID();
  return changed;
}

bool ComboRow(const char* label, int* current, std::span<const char* const> options, const char* help) {
  ImGui::PushID(label);
  PropertyLabel(label, help);
  bool changed = false;
  const int n = static_cast<int>(options.size());
  const char* preview = (*current >= 0 && *current < n) ? options[static_cast<size_t>(*current)] : "";
  if (ImGui::BeginCombo("##combo", preview)) {
    for (int i = 0; i < n; ++i) {
      const bool selected = i == *current;
      if (ImGui::Selectable(options[static_cast<size_t>(i)], selected)) {
        *current = i;
        changed = true;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return changed;
}

bool PrimaryButton(const char* label, const ImVec2& size) {
  const ThemeTokens& tk = tokens();
  ImGui::PushStyleColor(ImGuiCol_Button, tk.accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tk.accentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, tk.accentActive);
  ImGui::PushFont(fonts().semibold);
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopFont();
  ImGui::PopStyleColor(3);
  return pressed;
}

bool SecondaryButton(const char* label, const ImVec2& size) { return ImGui::Button(label, size); }

void ColormapBar(ImTextureID texture, const char* leftLabel, const char* rightLabel) {
  const ThemeTokens& tk = tokens();
  const float s = dpiScale();
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = 8.0f * s;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddImageRounded(texture, p, ImVec2(p.x + width, p.y + height), ImVec2(0, 0), ImVec2(1, 1),
                      IM_COL32_WHITE, height * 0.5f);
  ImGui::Dummy(ImVec2(width, height));
  ImGui::PushFont(fonts().caption);
  ImGui::PushStyleColor(ImGuiCol_Text, tk.textFaint);
  ImGui::TextUnformatted(leftLabel);
  const float rw = ImGui::CalcTextSize(rightLabel).x;
  ImGui::SameLine(ImGui::GetCursorStartPos().x + width - rw);
  ImGui::TextUnformatted(rightLabel);
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

void Caption(const char* text) {
  ImGui::PushFont(fonts().caption);
  ImGui::PushStyleColor(ImGuiCol_Text, tokens().textMuted);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

}  // namespace bh::ui
