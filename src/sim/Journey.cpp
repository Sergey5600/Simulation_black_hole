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

}  // namespace

bool Journey::named(const std::string& name, float startRadius, Journey& out) {
  out.samples_.clear();
  out.samples_.reserve(kSamples);
  if (name == "infall") buildInfall(startRadius, out.samples_);
  else if (name == "hover") buildHover(startRadius, out.samples_);
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
  return f;
}

}  // namespace bh
