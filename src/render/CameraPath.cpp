#include "render/CameraPath.hpp"

#include <algorithm>
#include <cmath>

namespace bh {

namespace {

float catmullRom(float p0, float p1, float p2, float p3, float t) {
  const float t2 = t * t, t3 = t2 * t;
  return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                 (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float smootherstep(float x) { return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f); }

}  // namespace

const char* CameraPath::names() { return "flyby, orbit, dive"; }

bool CameraPath::named(const std::string& name, const ViewPreset& base, CameraPath& out) {
  out.keys_.clear();
  if (name == "flyby") {
    out.keys_ = {
        {0.00f, {-0.40f, 0.95f, 180.0f}},
        {0.30f, {0.55f, 0.45f, 85.0f}},
        {0.60f, {1.75f, 0.10f, 45.0f}},
        {0.85f, {2.60f, 0.06f, 30.0f}},
        {1.00f, {3.10f, 0.16f, 24.0f}},
    };
  } else if (name == "orbit") {
    for (int i = 0; i <= 8; ++i) {
      const float u = static_cast<float>(i) / 8.0f;
      out.keys_.push_back({u, {base.yaw + 6.28318530718f * u, base.pitch, base.distance}});
    }
  } else if (name == "dive") {
    out.keys_ = {
        {0.0f, {base.yaw, base.pitch, 220.0f}},
        {0.7f, {base.yaw + 0.6f, base.pitch * 0.6f, 40.0f}},
        {1.0f, {base.yaw + 0.9f, base.pitch * 0.5f, 14.0f}},
    };
  } else {
    return false;
  }
  return true;
}

ViewPreset CameraPath::evaluate(float u) const {
  if (keys_.empty()) return ViewPreset{};
  u = smootherstep(std::clamp(u, 0.0f, 1.0f));
  size_t i = 0;
  while (i + 2 < keys_.size() && u > keys_[i + 1].u) ++i;
  const CameraKeyframe& k1 = keys_[i];
  const CameraKeyframe& k2 = keys_[std::min(i + 1, keys_.size() - 1)];
  const CameraKeyframe& k0 = keys_[i > 0 ? i - 1 : i];
  const CameraKeyframe& k3 = keys_[std::min(i + 2, keys_.size() - 1)];
  const float span = std::max(k2.u - k1.u, 1e-6f);
  const float t = std::clamp((u - k1.u) / span, 0.0f, 1.0f);

  ViewPreset v;
  v.yaw = catmullRom(k0.view.yaw, k1.view.yaw, k2.view.yaw, k3.view.yaw, t);
  v.pitch = catmullRom(k0.view.pitch, k1.view.pitch, k2.view.pitch, k3.view.pitch, t);
  v.distance = std::exp(catmullRom(std::log(k0.view.distance), std::log(k1.view.distance),
                                   std::log(k2.view.distance), std::log(k3.view.distance), t));
  return v;
}

}  // namespace bh
