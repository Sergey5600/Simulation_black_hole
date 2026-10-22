#pragma once

#include <glm/glm.hpp>

#include "sim/Appearance.hpp"

namespace bh {

// Orbit camera around the black hole with critically damped smoothing:
// input moves the target values, the visible values glide towards them.
class OrbitCamera {
 public:
  void setView(const ViewPreset& preset, bool instant);

  void rotate(float dxPixels, float dyPixels);
  void pan(float dxPixels, float dyPixels, float viewportHeight);
  void zoom(float wheelSteps);

  void update(float dt);

  glm::mat4 view() const;
  glm::mat4 projection(float aspect) const;
  glm::vec3 position() const;

  float fovDegrees = 40.0f;
  bool autoRotate = false;
  float autoRotateSpeed = 0.06f;  // rad/s
  float nearPlane = 0.05f;
  float farPlane = 5000.0f;

 private:
  struct Pose {
    float yaw = 0.0f, pitch = 0.2f, distance = 60.0f;
    glm::vec3 center{0.0f};
  };
  static glm::vec3 eye(const Pose& p);

  Pose target_;
  Pose current_;
};

}  // namespace bh
