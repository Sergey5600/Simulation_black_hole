#pragma once

#include <string>
#include <vector>

#include "sim/Appearance.hpp"

namespace bh {

// A smooth camera flight for recorded videos: keyframes on normalized time
// u in [0, 1], interpolated with Catmull-Rom splines (distance in log space)
// and eased in and out.
struct CameraKeyframe {
  float u;
  ViewPreset view;
};

class CameraPath {
 public:
  // Built-in paths: "flyby" (approach, rise over the disk, close-up of the
  // photon ring), "orbit" (one full turn around `base`), "dive" (straight in).
  static bool named(const std::string& name, const ViewPreset& base, CameraPath& out);
  static const char* names();

  ViewPreset evaluate(float u) const;

 private:
  std::vector<CameraKeyframe> keys_;
};

}  // namespace bh
