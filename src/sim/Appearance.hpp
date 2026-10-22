#pragma once

// Visual settings that belong to a scenario (each scenario remembers its own).
// Plain data, no GL: the renderer turns it into shader uniforms.
// The integer values of Quantity / Colormap / ColorStyle are mirrored in
// src/render/shaders/common.glsl — keep them in sync.
namespace bh {

enum class ColorStyle : int {
  Blackbody = 0,  // physical: color of a black body at the observed temperature
  Colormap = 1,   // scientific: map one quantity through a color map
};

enum class Quantity : int {
  Temperature = 0,      // disk emission temperature profile T(r)
  Radius = 1,           // Schwarzschild r
  Speed = 2,            // |β| measured by a local static observer
  Redshift = 3,         // g = ν_obs / ν_emit towards the camera
  TimeDilation = 4,     // dτ/dt
  Age = 5,              // photons: time since emission / lifetime (wave phase, 0..1)
  ImpactParameter = 6,  // photon b = L/E (captured if b < 3√3 M)
};

enum class Colormap : int { Nebula = 0, Inferno, Magma, Plasma, Viridis, Turbo, Ice, Count };

inline constexpr const char* kColormapNames[] = {"Nebula", "Inferno", "Magma", "Plasma", "Viridis", "Turbo", "Ice"};

struct Appearance {
  ColorStyle style = ColorStyle::Colormap;
  Quantity quantity = Quantity::Radius;
  Colormap colormap = Colormap::Nebula;
  float rangeMin = 0.0f;
  float rangeMax = 1.0f;  // may be below rangeMin to reverse the map

  float pointSize = 0.08f;   // sprite diameter in units of M
  float brightness = 1.0f;

  // Blackbody style.
  float peakTemperature = 6500.0f;  // K at the hottest radius of the disk
  bool dopplerBeaming = true;       // brightness ∝ g⁴
  bool colorShift = true;           // observed temperature = g · T

  bool fadeWithAge = false;  // photons: fade out before their slot is recycled
};

struct ViewPreset {
  float yaw = 0.6f;       // radians
  float pitch = 0.25f;    // radians above the disk plane
  float distance = 60.0f; // units of M
};

}  // namespace bh
