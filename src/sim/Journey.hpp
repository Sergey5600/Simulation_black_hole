#pragma once

#include <string>
#include <vector>

namespace bh {

// One moment of a first-person journey: the camera sits on a fixed radial line.
struct JourneyFrame {
  float r = 30.0f;           // Schwarzschild radius (M)
  bool hovering = false;     // engines on (a static observer); otherwise radial free fall
  float energy = 1.0f;       // E per unit mass of the free fall
  float gaze = 0.0f;         // angle between the view and the direction to the hole (rad), π = straight up
  double properTime = 0.0;   // τ since the start (M)
  double outsideTime = 0.0;  // time far away as seen in the sky overhead (M)
};

// Built-in journeys, sampled densely in u in [0, 1] and interpolated:
//   "infall": free fall from rest at infinity, from startRadius through the horizon to 0.5 M;
//             near the hole the gaze turns to the side so the shadow does not fill the frame.
//   "hover":  descend on engines to 2.01 M while turning to look straight up (the whole sky
//             shrinks into a bright blueshifted circle overhead and runs 14x faster), hover,
//             then cut the engines and fall through the horizon towards the singularity.
class Journey {
 public:
  static bool named(const std::string& name, float startRadius, Journey& out);
  static const char* names() { return "infall, hover"; }

  JourneyFrame at(float u) const;
  bool empty() const { return samples_.empty(); }

 private:
  std::vector<JourneyFrame> samples_;  // uniform in u
};

}  // namespace bh
