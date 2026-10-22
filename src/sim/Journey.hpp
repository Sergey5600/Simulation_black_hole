#pragma once

#include <string>
#include <vector>

#include "core/Vec3.hpp"

namespace bh {

// One moment of a first-person journey: the camera sits on a fixed radial line.
struct JourneyFrame {
  float r = 30.0f;           // Schwarzschild radius (M)
  bool hovering = false;     // engines on (a static observer); otherwise radial free fall
  float energy = 1.0f;       // E per unit mass of the free fall
  float gaze = 0.0f;         // angle between the view and the direction to the hole (rad), π = straight up
  float roll = 0.0f;         // 0: the frame's up is perpendicular to the radial line; 1: "outward" is up
  double properTime = 0.0;   // τ since the start (M)
  double outsideTime = 0.0;  // time far away as seen in the sky overhead (M)

  // Story ("peach") only.
  float cosmicTime = -1.0f;    // clock of the outside events: outside time / outside time at the release (< 0: none)
  bool peach = false;          // a peach floats next to the camera
  Vec3 peachOffset{};          // its center from the camera, meters, ship frame (radial out, side, up)
  float peachStretch = 1.0f;   // radial elongation once tides tear it (spaghettification)
  float peachSqueeze = 1.0f;   // transverse compression
  float peachSpin = 0.0f;      // slow tumble (rad)
  bool tornApart = false;      // tidal stress beyond the strength of peach flesh
};

// Built-in journeys, sampled densely in u in [0, 1] and interpolated:
//   "infall": free fall from rest at infinity, from startRadius through the horizon to 0.5 M;
//             near the hole the gaze turns to the side so the shadow does not fill the frame.
//   "hover":  descend on engines to 2.01 M while turning to look straight up (the whole sky
//             shrinks into a bright blueshifted circle overhead and runs 14x faster), hover,
//             then cut the engines and fall through the horizon towards the singularity.
//   "peach":  the hover journey as a story: a peach floats in front of the lens; while
//             hovering, the outside universe runs ahead (supernovae, a neutron star merger,
//             galaxies fading); after the release it drifts away on tides and, deep inside,
//             is torn into a noodle. The slow-motion ending reaches r = 0.0002 M.
// `solarMasses` sets the physical scale (tidal forces on a real 7.6 cm peach).
class Journey {
 public:
  static bool named(const std::string& name, float startRadius, Journey& out, double solarMasses = 4.3e6);
  static const char* names() { return "infall, hover, peach"; }

  JourneyFrame at(float u) const;
  bool empty() const { return samples_.empty(); }

 private:
  std::vector<JourneyFrame> samples_;  // uniform in u
};

}  // namespace bh
