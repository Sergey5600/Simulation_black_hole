#pragma once

#include <array>
#include <cstddef>
#include <vector>

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
  float skyBrightness = 1.0f;

  // Gravitational lensing (ray-traced background and thin disk).
  bool lensing = false;
  float spin = 0.0f;                 // a/M of the ray-traced hole (Kerr when != 0)
  int volumeResolution = 256;        // particle volume grid for lensing (N³ voxels)
  float volumeGlow = 1.0f;           // brightness of lensed particles
  bool diskAtIsco = true;            // inner edge follows the ISCO of the spin
  float diskInner = 6.0f;
  float diskOuter = 30.0f;
  float diskTemperature = 5800.0f;
  float diskBrightness = 1.0f;
  bool diskAutoExposure = true;      // compensate the much brighter inner disk of spinning holes
  bool diskBeaming = true;
  bool diskColorShift = true;
  bool particlesWhenLensing = false;  // overlay the (unlensed) particles
  int rayMaxSteps = 600;
  float rayStepScale = 0.035f;
  // > 0: ray trace in tiles of this size and wait for each, so a very heavy
  // frame never trips the Windows GPU watchdog (TDR, ~2 s per call).
  int rayTileSize = 0;

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
  float time = 0.0f;              // simulation time (animates the ray-traced disk)
};

// HDR point renderer: stars -> horizon -> guides -> additive particle splats
// -> bloom -> ACES tone mapping. Draws into the default framebuffer.
class Renderer {
 public:
  bool init();
  void shutdown();

  // Point buffer with room for `count` vertices. `generation` changes whenever
  // the storage is reallocated (GPU backends must re-register it then).
  struct PointBuffer {
    GLuint id = 0;
    uint64_t generation = 0;
  };
  PointBuffer reservePoints(size_t count);

  // CPU path: the simulation writes this frame's vertices straight into the
  // mapped GL buffer (no intermediate copy). Call end before render().
  PointVertex* beginPointUpload(size_t count);
  void endPointUpload();

  // Renders into `targetFbo` (0 = the window) of size in.width x in.height.
  void render(const FrameInput& in, const RenderSettings& settings, GLuint targetFbo = 0);

  // Offline helpers for video recording: adds `weight` x src into `dst`
  // (RGBA32F), and box-filters `src` down by `factor` into `dst`.
  void accumulate(GLuint srcTexture, const gl::RenderTarget& dst, float weight);
  void downsample(GLuint srcTexture, int factor, const gl::RenderTarget& dst);

  // 256x1 sRGB strips used as legends in the UI (ImTextureID-compatible).
  GLuint colormapTexture(Colormap c) const { return colormapStrips_[static_cast<size_t>(c)]; }

 private:
  void ensureTargets(int width, int height);
  void buildStars();
  void buildGuides();
  void buildColormapStrips();
  void buildSky();
  void renderBloom(const RenderSettings& settings);
  void setShadingUniforms(gl::Program& program, const FrameInput& in);
  bool splatParticles(const FrameInput& in, const RenderSettings& settings);

  gl::Program pointsProgram_, starsProgram_, horizonProgram_, linesProgram_;
  gl::Program bloomDownProgram_, bloomUpProgram_, compositeProgram_, stripProgram_;
  gl::Program skyGenProgram_, backgroundProgram_, splatProgram_, volumeResolveProgram_;
  gl::Program accumulateProgram_, downsampleProgram_;
  std::array<GLuint, 3> volumeSums_{};  // R32UI fixed-point light sums (r, g, b)
  GLuint volumeEmission_ = 0;          // RGBA16F emissivity, trilinear-filtered
  int volumeRes_ = 0;
  GLuint skyCubemap_ = 0;

  GLuint emptyVao_ = 0;
  GLuint pointsVao_ = 0, pointsVbo_ = 0;
  size_t pointsCapacity_ = 0;
  uint64_t pointsGeneration_ = 0;
  GLuint starsVao_ = 0, starsVbo_ = 0;
  GLsizei starCount_ = 0;
  GLuint linesVao_ = 0, linesVbo_ = 0;

  struct LineRange {
    GLint first = 0;
    GLsizei count = 0;
  };
  LineRange horizonRing_, photonRing_, iscoRing_, grid_;

  gl::RenderTarget hdr_;
  // Enough levels that the smallest is ~16 px tall, so the glow has the same
  // relative size at 1080p and at 8K.
  std::vector<gl::RenderTarget> bloomMips_;

  std::array<GLuint, static_cast<size_t>(Colormap::Count)> colormapStrips_{};
};

}  // namespace bh
