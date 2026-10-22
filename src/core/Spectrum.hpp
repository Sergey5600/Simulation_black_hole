#pragma once

#include <algorithm>
#include <cmath>

// Colors of black bodies, for the exact Doppler/gravitational shift of starlight.
// A black body of temperature T seen with frequency ratio g = ν_obs/ν_emit is
// again a black body, of temperature g T (I_ν / ν³ is invariant), so the
// observed color is just planckRgb(g T) times the source's dilution factor.
// In the visible band that brightens far slower than the bolometric g⁴: most of
// the extra energy of a blueshifted star lands in the ultraviolet and X-rays.
namespace bh::spectrum {

struct Rgb {
  double r, g, b;
};

inline double luminance(Rgb c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

// CIE 1931 2° color matching functions, multi-lobe Gaussian fit of
// Wyman, Sloan & Shirley (2013), wavelength in nm.
inline void cie1931(double nm, double& x, double& y, double& z) {
  auto g = [nm](double mu, double s1, double s2) {
    const double t = (nm - mu) / (nm < mu ? s1 : s2);
    return std::exp(-0.5 * t * t);
  };
  x = 1.056 * g(599.8, 37.9, 31.0) + 0.362 * g(442.0, 16.0, 26.7) - 0.065 * g(501.1, 20.4, 26.2);
  y = 0.821 * g(568.8, 46.9, 40.5) + 0.286 * g(530.9, 16.3, 31.1);
  z = 1.217 * g(437.0, 11.8, 36.0) + 0.681 * g(459.0, 26.0, 13.8);
}

// Planck's law integrated against the color matching functions (380-780 nm),
// converted to linear sRGB. Unnormalized; see planckRgb.
inline Rgb planckRgbRaw(double kelvin) {
  constexpr double kC2 = 1.4387769e-2;  // hc/k, m·K
  double X = 0.0, Y = 0.0, Z = 0.0;
  for (int i = 0; i <= 80; ++i) {
    const double nm = 380.0 + 5.0 * i;
    const double m = nm * 1e-9;
    const double planck = 1.0 / (m * m * m * m * m * std::expm1(std::min(kC2 / (m * kelvin), 700.0)));
    double x, y, z;
    cie1931(nm, x, y, z);
    X += planck * x;
    Y += planck * y;
    Z += planck * z;
  }
  return {3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z,
          0.0557 * X - 0.2040 * Y + 1.0570 * Z};
}

// Linear sRGB of a black body (per unit dilution), scaled so that a 6500 K body
// has luminance 1. Channels can be slightly negative where the Planckian locus
// leaves the sRGB gamut (deep reds below ~2000 K).
inline Rgb planckRgb(double kelvin) {
  static const double kNorm = 1.0 / luminance(planckRgbRaw(6500.0));
  const Rgb c = planckRgbRaw(kelvin);
  return {c.r * kNorm, c.g * kNorm, c.b * kNorm};
}

// The hue of a black body with its brightest channel at 1 (negative channels clipped).
inline Rgb planckColor(double kelvin) {
  const Rgb c = planckRgb(kelvin);
  const double m = std::max({c.r, c.g, c.b, 1e-300});
  return {std::max(c.r, 0.0) / m, std::max(c.g, 0.0) / m, std::max(c.b, 0.0) / m};
}

// Lookup table for the shaders: ln(planckRgb) at log10 T spread evenly over
// [kLutLogMin, kLutLogMax]. Logarithms keep the steep Wien tail exact under
// linear filtering.
constexpr int kLutSize = 1024;
constexpr double kLutLogMin = 2.0;  // 100 K
constexpr double kLutLogMax = 6.0;  // 10⁶ K

inline double lutTemperature(int i) {
  return std::pow(10.0, kLutLogMin + (kLutLogMax - kLutLogMin) * i / (kLutSize - 1));
}

}  // namespace bh::spectrum
