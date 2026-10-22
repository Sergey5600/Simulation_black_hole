#pragma once

#include <imgui.h>

#include <span>

// Reusable, styled controls. Panels (Ui.cpp) are built only from these plus a
// few plain ImGui calls, so restyling a widget restyles the whole app.
namespace bh::ui {

// iOS-style on/off switch drawn on the right side of a property row.
bool ToggleSwitch(const char* id, bool* value);

// Segmented control ("tabs" of equal width filling the row).
bool SegmentedControl(const char* id, int* current, std::span<const char* const> labels);

// Collapsible section with a small uppercase caption. Returns true when open;
// call EndSection() only in that case.
bool BeginSection(const char* title, bool defaultOpen = true);
void EndSection();

// Starts a "label | control" row: draws the muted label (with optional
// tooltip) and leaves the cursor at the control column with its width set.
void PropertyLabel(const char* label, const char* help = nullptr);

// Convenience rows built on PropertyLabel.
bool SliderRow(const char* label, float* v, float min, float max, const char* format = "%.2f", bool log = false,
               const char* help = nullptr);
bool SliderIntRow(const char* label, int* v, int min, int max, bool log = false, const char* help = nullptr);
bool ToggleRow(const char* label, bool* v, const char* help = nullptr);
bool ComboRow(const char* label, int* current, std::span<const char* const> options, const char* help = nullptr);

// Full-width buttons.
bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0));
bool SecondaryButton(const char* label, const ImVec2& size = ImVec2(0, 0));

// Rounded gradient legend for a color map texture, with end labels.
void ColormapBar(ImTextureID texture, const char* leftLabel, const char* rightLabel);

// Muted small text that wraps.
void Caption(const char* text);

}  // namespace bh::ui
