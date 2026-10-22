#pragma once

#include <algorithm>
#include <cmath>

// Radial motion near a Schwarzschild hole (G = c = M = 1) for the first-person
// journeys and their readout. An observer either hovers on engines (a static
// observer) or falls freely along a radius with energy per unit mass E:
// E = 1 falls from rest at infinity, E = sqrt(1 - 2M/r0) is dropped from rest at r0.
namespace bh::fall {

inline double lapse2(double r) { return 1.0 - 2.0 / r; }  // f = 1 - 2M/r = -g_tt

inline double energyFromRest(double r0) { return std::sqrt(std::max(lapse2(r0), 0.0)); }

// Rate of infall -dr/dτ = sqrt(E² - f). Finite at and inside the horizon.
inline double infallRate(double r, double e) { return std::sqrt(std::max(e * e - lapse2(r), 0.0)); }

// Speed relative to the static observer met on the way (outside the horizon only).
inline double localSpeed(double r, double e) { return std::sqrt(std::max(1.0 - lapse2(r) / (e * e), 0.0)); }

// Proper time left until r = 0 when falling inwards with energy E from radius r:
// τ = ∫₀^r dr' / sqrt(E² - 1 + 2/r').
inline double properTimeToSingularity(double r, double e) {
  const double k = e * e - 1.0;
  if (k < 0.0) {
    // Bound (dropped from rest at rmax = 2M/(1-E²)): the cycloid
    // r = (rmax/2)(1 + cos η), τ = (rmax/2)^1.5 (η + sin η).
    const double half = 1.0 / -k;
    const double eta = std::acos(std::clamp(r / half - 1.0, -1.0, 1.0));
    return half * std::sqrt(half) * (3.14159265358979323846 - eta - std::sin(eta));
  }
  // Unbound: with r' = r s² the integrand is smooth.
  auto integrand = [&](double s) { return 2.0 * r * s * s / std::sqrt(std::max(k * s * s + 2.0 / r, 1e-300)); };
  constexpr int kIntervals = 256;
  const double h = 1.0 / kIntervals;
  double sum = integrand(0.0) + integrand(1.0);
  for (int i = 1; i < kIntervals; ++i) sum += integrand(i * h) * (i % 2 ? 4.0 : 2.0);
  return sum * h / 3.0;
}

// Proper acceleration a rocket needs to hover at r: M / (r² sqrt(1 - 2M/r)).
inline double hoverAcceleration(double r) { return 1.0 / (r * r * std::sqrt(std::max(lapse2(r), 1e-300))); }

// Tidal acceleration per unit length: radial stretching 2M/r³ (the transverse squeeze is half that).
inline double tidalStretch(double r) { return 2.0 / (r * r * r); }

// Angular radius of the sky around the outward direction seen by a static
// observer; the rest of the view is the shadow. Inside the photon sphere the
// whole universe shrinks into a circle overhead: sin ψ = (3√3 M / r) sqrt(1 - 2M/r).
inline double staticSkyRadius(double r) {
  const double s = std::min(3.0 * std::sqrt(3.0) / r * std::sqrt(std::max(lapse2(r), 0.0)), 1.0);
  const double psi = std::asin(s);
  return r < 3.0 ? psi : 3.14159265358979323846 - psi;
}

// Frequency ratio ν_observed / ν_emitted for light from far away arriving
// straight from overhead (radially outward), which is also how fast the outside
// universe seems to run. Hovering: 1/sqrt(f) for every direction.
inline double overheadShiftStatic(double r) { return 1.0 / std::sqrt(std::max(lapse2(r), 1e-300)); }
inline double overheadShiftFalling(double r, double e) { return 1.0 / (e + infallRate(r, e)); }

// Geometric units of a hole of `solarMasses` in SI.
struct Units {
  double length;  // m per M
  double time;    // s per M
  double accel;   // m/s² per c²/M
  explicit Units(double solarMasses)
      : length(1476.625 * solarMasses), time(4.925491e-6 * solarMasses), accel(8.987551787e16 / length) {}
};

}  // namespace bh::fall
