#include "render/Camera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace bh {

namespace {
constexpr float kMinDistance = 4.0f;
constexpr float kMaxDistance = 1500.0f;
constexpr float kMaxPitch = 1.55f;
}  // namespace

void OrbitCamera::setView(const ViewPreset& preset, bool instant) {
  target_.yaw = preset.yaw;
  target_.pitch = preset.pitch;
  target_.distance = preset.distance;
  target_.center = glm::vec3(0.0f);
  if (instant) current_ = target_;
}

void OrbitCamera::rotate(float dxPixels, float dyPixels) {
  target_.yaw -= dxPixels * 0.005f;
  target_.pitch = std::clamp(target_.pitch + dyPixels * 0.005f, -kMaxPitch, kMaxPitch);
}

void OrbitCamera::pan(float dxPixels, float dyPixels, float viewportHeight) {
  const glm::mat4 v = view();
  const glm::vec3 right(v[0][0], v[1][0], v[2][0]);
  const glm::vec3 up(v[0][1], v[1][1], v[2][1]);
  const float unitsPerPixel =
      2.0f * current_.distance * std::tan(glm::radians(fovDegrees) * 0.5f) / std::max(viewportHeight, 1.0f);
  target_.center += (-right * dxPixels + up * dyPixels) * unitsPerPixel;
}

void OrbitCamera::zoom(float wheelSteps) {
  target_.distance = std::clamp(target_.distance * std::pow(0.88f, wheelSteps), kMinDistance, kMaxDistance);
}

void OrbitCamera::update(float dt) {
  if (autoRotate) target_.yaw += autoRotateSpeed * dt;
  // Frame-rate independent exponential smoothing.
  const float k = 1.0f - std::exp(-dt * 10.0f);
  current_.yaw += (target_.yaw - current_.yaw) * k;
  current_.pitch += (target_.pitch - current_.pitch) * k;
  current_.distance *= std::pow(target_.distance / current_.distance, k);
  current_.center += (target_.center - current_.center) * k;
}

glm::vec3 OrbitCamera::eye(const Pose& p) {
  const float cp = std::cos(p.pitch);
  return p.center + p.distance * glm::vec3(cp * std::sin(p.yaw), std::sin(p.pitch), cp * std::cos(p.yaw));
}

glm::vec3 OrbitCamera::position() const { return eye(current_); }

glm::mat4 OrbitCamera::view() const {
  return glm::lookAt(eye(current_), current_.center, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrbitCamera::projection(float aspect) const {
  return glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
}

}  // namespace bh
