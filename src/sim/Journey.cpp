#include "sim/Journey.hpp"

#include <algorithm>
#include <cmath>

#include "core/Fall.hpp"

namespace bh {

namespace {

constexpr int kSamples = 4096;
constexpr double kPi = 3.14159265358979323846;

double smoothstep(double a, double b, double x) {
  const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

double smootherstep(double x) {
  x = std::clamp(x, 0.0, 1.0);
  return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

// Free fall from rest at infinity, r = r0 (0.5 / r0)^u.
void buildInfall(float startRadius, std::vector<JourneyFrame>& out) {
  const double r0 = std::max(static_cast<double>(startRadius), 4.0), r1 = 0.5;
  JourneyFrame prev;
  double prevR = r0;  // in double: float radii differ by less than their rounding near the end
  for (int i = 0; i < kSamples; ++i) {
    const double u = static_cast<double>(i) / (kSamples - 1);
    JourneyFrame f;
    const double r = r0 * std::pow(r1 / r0, u);
    f.r = static_cast<float>(r);
    f.energy = 1.0f;
    f.gaze = static_cast<float>(1.15 * smoothstep(14.0, 1.5, r));
    if (i > 0) {
      const double rm = 0.5 * (r + prevR);
      const double dtau = (prevR - r) / fall::infallRate(rm, 1.0);
      f.properTime = prev.properTime + dtau;
      f.outsideTime = prev.outsideTime + dtau * fall::overheadShiftFalling(rm, 1.0);
    }
    out.push_back(f);
    prev = f;
    prevR = r;
  }
}

// Descend on engines, hover, then drop from rest.
void buildHover(float startRadius, std::vector<JourneyFrame>& out) {
  const double r0 = std::max(static_cast<double>(startRadius), 6.0);
  constexpr double kHover = 2.01;           // sky circle 10.5° across, clocks x14
  constexpr double kDescentSpeed = 0.3;     // relative to the static observers passed (c)
  constexpr double kHoverTime = 60.0;       // proper time spent hovering (M)
  constexpr double kEndRadius = 0.25;
  constexpr double kDescentEnd = 0.40, kHoverEnd = 0.62;

  // Drop from rest at kHover: r'' = -M/r² in proper time (the first-order form
  // has a fixed point at the release). Integrate once to find the duration.
  const double energy = fall::energyFromRest(kHover);
  struct Drop {
    double r, v;
  };
  auto dropStep = [](Drop d, double h) {
    auto acc = [](double r) { return -1.0 / (r * r); };
    const double k1r = d.v, k1v = acc(d.r);
    const double k2r = d.v + 0.5 * h * k1v, k2v = acc(d.r + 0.5 * h * k1r);
    const double k3r = d.v + 0.5 * h * k2v, k3v = acc(d.r + 0.5 * h * k2r);
    const double k4r = d.v + h * k3v, k4v = acc(d.r + h * k3r);
    return Drop{d.r + h / 6.0 * (k1r + 2.0 * k2r + 2.0 * k3r + k4r), d.v + h / 6.0 * (k1v + 2.0 * k2v + 2.0 * k3v + k4v)};
  };
  constexpr double kDropStep = 1e-4;
  double dropDuration = 0.0;
  for (Drop d{kHover, 0.0}; d.r > kEndRadius; d = dropStep(d, kDropStep)) dropDuration += kDropStep;

  Drop drop{kHover, 0.0};
  double dropTime = 0.0;   // proper time of `drop` since the release
  double dropDrawn = 0.0;  // proper time of the previous drop sample
  JourneyFrame prev;
  double prevR = r0;
  for (int i = 0; i < kSamples; ++i) {
    const double u = static_cast<double>(i) / (kSamples - 1);
    JourneyFrame f;
    if (u <= kHoverEnd) {
      // r - 2M shrinks geometrically, easing into the hover.
      const double w = smootherstep(u / kDescentEnd);
      const double r = 2.0 + (r0 - 2.0) * std::pow((kHover - 2.0) / (r0 - 2.0), w);
      f.r = static_cast<float>(r);
      f.hovering = true;
      f.energy = static_cast<float>(energy);
      f.gaze = static_cast<float>(0.12 + (kPi - 0.12) * smoothstep(0.3, 1.0, w));
      if (i > 0) {
        const double rm = 0.5 * (r + prevR);
        // Descent: proper distance dr / sqrt(f) at kDescentSpeed; hover: plain waiting.
        double dtau = (prevR - r) / (std::sqrt(fall::lapse2(rm)) * kDescentSpeed);
        if (u > kDescentEnd) dtau = kHoverTime / ((kHoverEnd - kDescentEnd) * (kSamples - 1));
        f.properTime = prev.properTime + dtau;
        f.outsideTime = prev.outsideTime + dtau * fall::overheadShiftStatic(rm);
      }
      prevR = r;
    } else {
      const double target = (u - kHoverEnd) / (1.0 - kHoverEnd) * dropDuration;
      while (dropTime + kDropStep <= target) {
        drop = dropStep(drop, kDropStep);
        dropTime += kDropStep;
      }
      const Drop d = dropStep(drop, target - dropTime);
      f.r = static_cast<float>(std::max(d.r, kEndRadius));
      f.energy = static_cast<float>(energy);
      f.gaze = static_cast<float>(kPi);
      const double dtau = target - std::max(dropDrawn, 0.0);
      dropDrawn = target;
      const double r = std::max(d.r, kEndRadius);
      const double rm = 0.5 * (r + prevR);
      prevR = r;
      f.properTime = prev.properTime + dtau;
      f.outsideTime = prev.outsideTime + dtau * fall::overheadShiftFalling(rm, energy);
    }
    out.push_back(f);
    prev = f;
  }
}

// The story: hover with a peach held in front of the lens, then both drop.
//   descent [0, 0.34] -> hover [0.34, 0.58] -> fall to r = 0.3 M [0.58, 0.82]
//   -> slow motion, r shrinking geometrically to 0.0002 M [0.82, 1].
// After the release the peach follows the geodesic deviation equation
// ξ'' = K ξ with the tidal tensor K = diag(2, -1, -1) M/r³ (radial, side, up) in
// SI units, while the ship holds station within 0.9 m. Tides would line the
// peach up straight above the camera, so before they get violent the ship
// moves beside it (thrusters) and the camera looks sideways: the stretch then
// runs up and down the frame. Once the tidal stress across the peach exceeds
// the strength of its flesh it stretches the same way, radially.
void buildPeach(float startRadius, double solarMasses, std::vector<JourneyFrame>& out) {
  const double r0 = std::max(static_cast<double>(startRadius), 6.0);
  constexpr double kHover = 2.01, kDescentSpeed = 0.3, kHoverTime = 60.0;
  constexpr double kDescentEnd = 0.34, kHoverEnd = 0.58, kSlowMotion = 0.82;
  constexpr double kSlowRadius = 0.3, kEndRadius = 2e-4;
  // A peach: 7.6 cm across, flesh ~1000 kg/m³ that tears at ~0.1 MPa. Tides
  // stretch it with stress ~ ρ K ℓ² / 4, so it tears once K > 4σ / (ρ ℓ²).
  constexpr double kPeachSize = 0.076, kDensity = 1000.0, kStrength = 1e5;
  constexpr double kTearRate = 4.0 * kStrength / (kDensity * kPeachSize * kPeachSize);  // s⁻²
  constexpr double kStation = 0.9;                 // m
  const Vec3 kBeside{0.12f, 0.55f, -0.05f};        // where the ship finally holds the peach (m)
  constexpr double kMoveStart = 0.78, kMoveEnd = 0.86;
  constexpr double kHoverGaze = kPi - 0.3;
  constexpr double kFinale = 0.94;
  const fall::Units units(solarMasses);
  const double tidalUnit = units.accel / units.length;  // s⁻² per (M/r³ = 1)
  const double energy = fall::energyFromRest(kHover);

  // Drop from rest at kHover is a cycloid: r = (R/2)(1 + cos η), τ = (R/2)^1.5 (η + sin η).
  auto cycloidRadius = [&](double eta) { return 0.5 * kHover * (1.0 + std::cos(eta)); };
  auto cycloidTime = [&](double eta) { return std::pow(0.5 * kHover, 1.5) * (eta + std::sin(eta)); };
  auto etaAt = [&](double r) { return std::acos(std::clamp(2.0 * r / kHover - 1.0, -1.0, 1.0)); };
  const double etaSlow = etaAt(kSlowRadius);

  // Held in front of the lens (camera frame: forward, right, up) until the release.
  auto heldOffset = [](double gaze) {
    const Vec3 forward{static_cast<float>(-std::cos(gaze)), static_cast<float>(std::sin(gaze)), 0.0f};
    const Vec3 right{static_cast<float>(std::sin(gaze)), static_cast<float>(std::cos(gaze)), 0.0f};
    return forward * 0.45f + right * 0.10f + Vec3{0.0f, 0.0f, -0.07f};
  };

  // Geodesic deviation state (SI, ship frame) after the release.
  Vec3 xi{}, xiRate{};
  double stretch = 1.0, stretchRate = 0.0, squeeze = 1.0, squeezeRate = 0.0;
  bool torn = false;
  double tauDeviation = 0.0;  // proper time (M) the deviation state has reached
  auto advanceDeviation = [&](double tauTarget, double eta0, double eta1) {
    // Integrate in small proper-time steps; r along the cycloid between the samples.
    while (tauDeviation < tauTarget) {
      double lo = eta0, hi = eta1;  // invert τ(η) by bisection
      for (int k = 0; k < 40; ++k) {
        const double mid = 0.5 * (lo + hi);
        (cycloidTime(mid) < tauDeviation ? lo : hi) = mid;
      }
      const double r = cycloidRadius(lo);
      const double kr = 2.0 / (r * r * r) * tidalUnit;  // s⁻²
      const double dtSi = std::min((tauTarget - tauDeviation) * units.time, 0.02 / std::sqrt(kr));
      const float h = static_cast<float>(dtSi);
      // Semi-implicit Euler: ξ' += K ξ dt, ξ += ξ' dt.
      xiRate += Vec3{static_cast<float>(kr) * xi.x, static_cast<float>(-0.5 * kr) * xi.y,
                     static_cast<float>(-0.5 * kr) * xi.z} * h;
      xi += xiRate * h;
      const float dist = length(xi);
      if (dist > kStation) {  // the ship's thrusters keep it close
        const Vec3 n = xi / dist;
        xi = n * static_cast<float>(kStation);
        const float away = dot(xiRate, n);
        if (away > 0.0f) xiRate -= n * away;
      }
      if (!torn && kr > kTearRate) torn = true;
      if (torn) {
        stretchRate += kr * stretch * dtSi;
        stretch += stretchRate * dtSi;
        squeezeRate += -0.5 * kr * squeeze * dtSi;
        squeeze = std::max(squeeze + squeezeRate * dtSi, 0.02);
      }
      tauDeviation += dtSi / units.time;
    }
  };

  JourneyFrame prev;
  double prevR = r0;
  double prevEta = 0.0;
  for (int i = 0; i < kSamples; ++i) {
    const double u = static_cast<double>(i) / (kSamples - 1);
    JourneyFrame f;
    f.peach = true;
    f.energy = static_cast<float>(energy);
    f.peachSpin = static_cast<float>(2.4 * u);
    double r = kHover;
    if (u <= kHoverEnd) {
      const double w = smootherstep(u / kDescentEnd);
      r = 2.0 + (r0 - 2.0) * std::pow((kHover - 2.0) / (r0 - 2.0), w);
      f.hovering = true;
      // Up, 17° off the zenith: the sky circle sits above the center and its
      // light grazes the peach instead of only backlighting it.
      f.gaze = static_cast<float>(0.12 + (kHoverGaze - 0.12) * smoothstep(0.3, 1.0, w));
      f.peachOffset = heldOffset(f.gaze);
      if (i > 0) {
        const double rm = 0.5 * (r + prevR);
        double dtau = (prevR - r) / (std::sqrt(fall::lapse2(rm)) * kDescentSpeed);
        if (u > kDescentEnd) dtau = kHoverTime / ((kHoverEnd - kDescentEnd) * (kSamples - 1));
        f.properTime = prev.properTime + dtau;
        f.outsideTime = prev.outsideTime + dtau * fall::overheadShiftStatic(rm);
      }
      xi = f.peachOffset;
    } else {
      double eta;
      if (u <= kSlowMotion) {
        eta = etaSlow * (u - kHoverEnd) / (kSlowMotion - kHoverEnd);
      } else {
        const double s = (u - kSlowMotion) / (1.0 - kSlowMotion);
        eta = etaAt(kSlowRadius * std::pow(kEndRadius / kSlowRadius, s));
      }
      r = cycloidRadius(eta);
      const double tau = cycloidTime(eta);
      advanceDeviation(tau, prevEta, eta);
      prevEta = eta;
      const double dtau = tau - (prev.hovering ? 0.0 : cycloidTime(etaAt(prevR)));
      const double rm = 0.5 * (r + prevR);
      f.properTime = prev.properTime + std::max(dtau, 0.0);
      f.outsideTime = prev.outsideTime + std::max(dtau, 0.0) * fall::overheadShiftFalling(rm, energy);
      if (u > kMoveStart) {  // thrusters: move beside the peach, then hold it there
        const float m = static_cast<float>(smoothstep(kMoveStart, kMoveEnd, u));
        xi = xi + (kBeside - xi) * m;
        xiRate = xiRate * (1.0f - m);
      }
      f.peachOffset = xi;
      f.peachStretch = static_cast<float>(stretch);
      f.peachSqueeze = static_cast<float>(squeeze);
      f.tornApart = torn;
      // The camera turns to keep the peach just left of the center.
      const double track = std::atan2(static_cast<double>(xi.y), -static_cast<double>(xi.x)) + 0.12;
      const double tracked = track < 0.0 ? track + 2.0 * kPi : track;
      f.gaze = static_cast<float>(kHoverGaze + (tracked - kHoverGaze) * smoothstep(kHoverEnd, kHoverEnd + 0.06, u));
      f.roll = static_cast<float>(smoothstep(kMoveStart, kMoveEnd, u));
      f.singularity = static_cast<float>(std::clamp((u - kFinale) / (1.0 - kFinale), 0.0, 1.0));
    }
    f.r = static_cast<float>(r);
    out.push_back(f);
    prev = f;
    prevR = r;
  }
  // The story clock of the outside events: 1 at the release.
  double atRelease = 1.0;
  for (const JourneyFrame& f : out)
    if (f.hovering) atRelease = f.outsideTime;
  for (JourneyFrame& f : out) f.cosmicTime = static_cast<float>(f.outsideTime / atRelease);
}

}  // namespace

bool Journey::named(const std::string& name, float startRadius, Journey& out, double solarMasses) {
  out.samples_.clear();
  out.samples_.reserve(kSamples);
  if (name == "infall") buildInfall(startRadius, out.samples_);
  else if (name == "hover") buildHover(startRadius, out.samples_);
  else if (name == "peach") buildPeach(startRadius, solarMasses, out.samples_);
  else return false;
  return true;
}

JourneyFrame Journey::at(float u) const {
  if (samples_.empty()) return {};
  const float x = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(samples_.size() - 1);
  const size_t i = std::min(static_cast<size_t>(x), samples_.size() - 2);
  const float t = x - static_cast<float>(i);
  const JourneyFrame& a = samples_[i];
  const JourneyFrame& b = samples_[i + 1];
  JourneyFrame f = t < 0.5f ? a : b;  // discrete state from the nearest sample
  f.r = a.r + (b.r - a.r) * t;
  f.gaze = a.gaze + (b.gaze - a.gaze) * t;
  f.properTime = a.properTime + (b.properTime - a.properTime) * t;
  f.outsideTime = a.outsideTime + (b.outsideTime - a.outsideTime) * t;
  f.cosmicTime = a.cosmicTime + (b.cosmicTime - a.cosmicTime) * t;
  f.peachOffset = a.peachOffset + (b.peachOffset - a.peachOffset) * t;
  f.peachStretch = a.peachStretch + (b.peachStretch - a.peachStretch) * t;
  f.peachSqueeze = a.peachSqueeze + (b.peachSqueeze - a.peachSqueeze) * t;
  f.peachSpin = a.peachSpin + (b.peachSpin - a.peachSpin) * t;
  f.roll = a.roll + (b.roll - a.roll) * t;
  f.singularity = a.singularity + (b.singularity - a.singularity) * t;
  return f;
}

}  // namespace bh
