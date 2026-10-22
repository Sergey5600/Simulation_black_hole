#pragma once

#include <array>
#include <cstddef>

#include "render/GlUtil.hpp"
#include "sim/Appearance.hpp"
#include "sim/Particle.hpp"

namespace bh {

// Global look of the frame (independent of the scenario).
struct RenderSettings {
  float exposure = 1.0f;
  bool bloom = true;
  float bloomStrength = 0.08f;
  float bloomRadius = 1.0f;
  float vignette = 0.35f;

  bool stars = true;
  bool guideHorizon = false;
  bool guidePhotonSphere = false;
  bool guideIsco = false;
  bool guideGrid = false;
};

struct FrameInput {
  size_t pointCount = 0;
  bool photons = false;           // particles are light, not gas
  bool scalarIsBrightness = false; // PointVertex::scalar modulates brightness
  Appearance appearance;
  float lifetime = 0.0f;          // photon slot lifetime: normalizes Age, drives fading

  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};
  glm::vec3 cameraPos{0.0f};
  float fovYRadians = 0.7f;
  int width = 1;                  // framebuffer pixels
  int height = 1;
  float dpiScale = 1.0f;
};

// HDR point renderer: stars -> horizon -> guides -> additive particle splats
// -> bloom -> ACES tone mapping. Draws into the default framebuffer.
class Renderer {
 public:
  bool init();
  void shutdown();

  // The simulation writes this frame's vertices straight into the mapped GL
  // buffer (no intermediate copy). Call end before render().
  PointVertex* beginPointUpload(size_t count);
  void endPointUpload();

  void render(const FrameInput& in, const RenderSettings& settings);

  // 256x1 sRGB strips used as legends in the UI (ImTextureID-compatible).
  GLuint colormapTexture(Colormap c) const { return colormapStrips_[static_cast<size_t>(c)]; }

 private:
  void ensureTargets(int width, int height);
  void buildStars();
  void buildGuides();
  void buildColormapStrips();
  void renderBloom(const RenderSettings& settings);

  gl::Program pointsProgram_, starsProgram_, horizonProgram_, linesProgram_;
  gl::Program bloomDownProgram_, bloomUpProgram_, compositeProgram_, stripProgram_;

  GLuint emptyVao_ = 0;
  GLuint pointsVao_ = 0, pointsVbo_ = 0;
  size_t pointsCapacity_ = 0;
  GLuint starsVao_ = 0, starsVbo_ = 0;
  GLsizei starCount_ = 0;
  GLuint linesVao_ = 0, linesVbo_ = 0;

  struct LineRange {
    GLint first = 0;
    GLsizei count = 0;
  };
  LineRange horizonRing_, photonRing_, iscoRing_, grid_;

  gl::RenderTarget hdr_;
  static constexpr int kBloomLevels = 6;
  std::array<gl::RenderTarget, kBloomLevels> bloomMips_;

  std::array<GLuint, static_cast<size_t>(Colormap::Count)> colormapStrips_{};
};

}  // namespace bh
