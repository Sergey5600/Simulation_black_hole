#include "render/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "EmbeddedShaders.hpp"
#include "core/Kerr.hpp"
#include "core/Random.hpp"
#include "core/Schwarzschild.hpp"

namespace bh {

namespace {

void appendRing(std::vector<glm::vec3>& out, float radius, int segments) {
  for (int i = 0; i < segments; ++i) {
    const float a = 6.28318530718f * static_cast<float>(i) / static_cast<float>(segments);
    out.emplace_back(radius * std::cos(a), 0.0f, radius * std::sin(a));
  }
}

}  // namespace

bool Renderer::init() {
  using namespace shaders;
  bool ok = true;
  ok &= pointsProgram_.build("points", {common, particle_shading, points_vert}, {points_frag});
  ok &= starsProgram_.build("stars", {stars_vert}, {points_frag});
  ok &= horizonProgram_.build("horizon", {fullscreen_vert}, {horizon_frag});
  ok &= linesProgram_.build("lines", {lines_vert}, {lines_frag});
  ok &= bloomDownProgram_.build("bloom_down", {fullscreen_vert}, {bloom_down_frag});
  ok &= bloomUpProgram_.build("bloom_up", {fullscreen_vert}, {bloom_up_frag});
  ok &= compositeProgram_.build("composite", {fullscreen_vert}, {composite_frag});
  ok &= stripProgram_.build("colormap_strip", {fullscreen_vert}, {common, colormap_strip_frag});
  ok &= skyGenProgram_.build("sky_gen", {fullscreen_vert}, {common, sky_gen_frag});
  ok &= backgroundProgram_.build("lensing", {fullscreen_vert}, {common, lensing_frag});
  ok &= splatProgram_.buildCompute("splat", {common, particle_shading, splat_comp});
  ok &= volumeResolveProgram_.buildCompute("volume_resolve", {volume_resolve_comp});
  if (!ok) return false;

  glGenVertexArrays(1, &emptyVao_);

  glGenVertexArrays(1, &pointsVao_);
  glGenBuffers(1, &pointsVbo_);
  glBindVertexArray(pointsVao_);
  glBindBuffer(GL_ARRAY_BUFFER, pointsVbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(PointVertex), reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(PointVertex), reinterpret_cast<void*>(16));
  glBindVertexArray(0);

  buildStars();
  buildGuides();
  buildColormapStrips();
  buildSky();
  return true;
}

void Renderer::shutdown() {
  hdr_.destroy();
  for (auto& m : bloomMips_) m.destroy();
  glDeleteTextures(static_cast<GLsizei>(colormapStrips_.size()), colormapStrips_.data());
  glDeleteTextures(1, &skyCubemap_);
  glDeleteTextures(3, volumeSums_.data());
  glDeleteTextures(1, &volumeEmission_);
  const GLuint vaos[] = {emptyVao_, pointsVao_, starsVao_, linesVao_};
  const GLuint vbos[] = {pointsVbo_, starsVbo_, linesVbo_};
  glDeleteVertexArrays(4, vaos);
  glDeleteBuffers(3, vbos);
}

Renderer::PointBuffer Renderer::reservePoints(size_t count) {
  glBindBuffer(GL_ARRAY_BUFFER, pointsVbo_);
  if (count > pointsCapacity_) {
    pointsCapacity_ = std::max(count, pointsCapacity_ + pointsCapacity_ / 2);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(pointsCapacity_ * sizeof(PointVertex)), nullptr,
                 GL_STREAM_DRAW);
    ++pointsGeneration_;
  }
  return {pointsVbo_, pointsGeneration_};
}

PointVertex* Renderer::beginPointUpload(size_t count) {
  reservePoints(count);
  if (count == 0) return nullptr;
  return static_cast<PointVertex*>(
      glMapBufferRange(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(count * sizeof(PointVertex)),
                       GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));
}

void Renderer::endPointUpload() {
  glBindBuffer(GL_ARRAY_BUFFER, pointsVbo_);
  GLint mapped = 0;
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAPPED, &mapped);
  if (mapped) glUnmapBuffer(GL_ARRAY_BUFFER);
}

void Renderer::buildStars() {
  struct Star {
    float dir[3];
    float brightness;
    float color[3];
  };
  constexpr int kStars = 9000;
  std::vector<Star> stars(kStars);
  for (int i = 0; i < kStars; ++i) {
    Rng rng(static_cast<uint32_t>(i), 0xC0FFEEu);
    const float z = rng.uniform(-1.0f, 1.0f);
    const float phi = rng.uniform(0.0f, 6.28318530718f);
    const float rho = std::sqrt(1.0f - z * z);
    const float u = rng.uniform();
    const float brightness = 0.04f + 0.96f * std::pow(u, 14.0f);  // most stars are faint
    const float warm = rng.uniform();
    const glm::vec3 tint = glm::mix(glm::vec3(0.62f, 0.75f, 1.0f), glm::vec3(1.0f, 0.78f, 0.55f), warm);
    Star& s = stars[static_cast<size_t>(i)];
    s.dir[0] = rho * std::cos(phi);
    s.dir[1] = z;
    s.dir[2] = rho * std::sin(phi);
    s.brightness = brightness;
    s.color[0] = tint.x * 0.6f;
    s.color[1] = tint.y * 0.6f;
    s.color[2] = tint.z * 0.6f;
  }
  starCount_ = kStars;

  glGenVertexArrays(1, &starsVao_);
  glGenBuffers(1, &starsVbo_);
  glBindVertexArray(starsVao_);
  glBindBuffer(GL_ARRAY_BUFFER, starsVbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(stars.size() * sizeof(Star)), stars.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Star), reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Star), reinterpret_cast<void*>(16));
  glBindVertexArray(0);
}

void Renderer::buildGuides() {
  std::vector<glm::vec3> v;
  auto ring = [&](float r, LineRange& range) {
    range.first = static_cast<GLint>(v.size());
    appendRing(v, r, 256);
    range.count = static_cast<GLsizei>(v.size()) - range.first;
  };
  ring(gr::kHorizon, horizonRing_);
  ring(gr::kPhotonSphere, photonRing_);
  ring(gr::kIsco, iscoRing_);

  // Polar grid in the equatorial plane: rings every 10 M plus 12 spokes (GL_LINES).
  grid_.first = static_cast<GLint>(v.size());
  for (int r = 10; r <= 100; r += 10) {
    const size_t start = v.size();
    appendRing(v, static_cast<float>(r), 256);
    // Convert the loop into line segments.
    std::vector<glm::vec3> seg;
    for (size_t i = start; i < v.size(); ++i) {
      seg.push_back(v[i]);
      seg.push_back(v[i + 1 < v.size() ? i + 1 : start]);
    }
    v.resize(start);
    v.insert(v.end(), seg.begin(), seg.end());
  }
  for (int i = 0; i < 12; ++i) {
    const float a = 6.28318530718f * static_cast<float>(i) / 12.0f;
    const glm::vec3 d(std::cos(a), 0.0f, std::sin(a));
    v.push_back(d * 10.0f);
    v.push_back(d * 100.0f);
  }
  grid_.count = static_cast<GLsizei>(v.size()) - grid_.first;

  glGenVertexArrays(1, &linesVao_);
  glGenBuffers(1, &linesVbo_);
  glBindVertexArray(linesVao_);
  glBindBuffer(GL_ARRAY_BUFFER, linesVbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(glm::vec3)), v.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void*>(0));
  glBindVertexArray(0);
}

void Renderer::buildColormapStrips() {
  constexpr int kWidth = 256;
  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glGenTextures(static_cast<GLsizei>(colormapStrips_.size()), colormapStrips_.data());
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, kWidth, 1);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  stripProgram_.use();
  glBindVertexArray(emptyVao_);
  for (size_t i = 0; i < colormapStrips_.size(); ++i) {
    glBindTexture(GL_TEXTURE_2D, colormapStrips_[i]);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, kWidth, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colormapStrips_[i], 0);
    stripProgram_.set("uColormap", static_cast<int>(i));
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
}

// Bakes the sky (Milky Way band + the star points) into a cube map once, so the
// lensing shader can look up any escaped ray direction.
void Renderer::buildSky() {
  constexpr int kFace = 2048;  // sharp stars even when the lens magnifies them in 4K
  glGenTextures(1, &skyCubemap_);
  glBindTexture(GL_TEXTURE_CUBE_MAP, skyCubemap_);
  glTexStorage2D(GL_TEXTURE_CUBE_MAP, 1, GL_RGBA16F, kFace, kFace);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

  // Standard OpenGL cube-map face orientations.
  const glm::vec3 targets[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  const glm::vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
  const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, kFace, kFace);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_PROGRAM_POINT_SIZE);
  for (int face = 0; face < 6; ++face) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, skyCubemap_,
                           0);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), targets[face], ups[face]);

    glDisable(GL_BLEND);
    skyGenProgram_.use();
    skyGenProgram_.set("uInvViewProj", glm::inverse(proj * view));
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    starsProgram_.use();
    starsProgram_.set("uViewRotation", view);
    starsProgram_.set("uProj", proj);
    starsProgram_.set("uScale", 1.2f * static_cast<float>(kFace) / 1024.0f);  // same angular size
    glBindVertexArray(starsVao_);
    glDrawArrays(GL_POINTS, 0, starCount_);
  }
  glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
}

void Renderer::setShadingUniforms(gl::Program& program, const FrameInput& in) {
  const Appearance& a = in.appearance;
  program.set("uCameraPos", in.cameraPos);
  program.set("uPointSize", a.pointSize);
  program.set("uPhotons", in.photons);
  program.set("uScalarIsBrightness", in.scalarIsBrightness);
  program.set("uStyle", static_cast<int>(a.style));
  program.set("uQuantity", static_cast<int>(a.quantity));
  program.set("uColormap", static_cast<int>(a.colormap));
  const float span = std::fabs(a.rangeMax - a.rangeMin) < 1e-6f ? 1e-6f : a.rangeMax - a.rangeMin;
  program.set("uRange", glm::vec2(a.rangeMin, a.rangeMin + span));
  // Gas: total light must not depend on how finely we sample it. Each particle
  // carries 1/N of the disk and its sprite covers ∝ size² pixels.
  float brightness = a.brightness;
  if (!in.photons) {
    constexpr float kRefCount = 300000.0f, kRefSize = 0.12f;
    const float sizeRatio = kRefSize / std::max(a.pointSize, 1e-4f);
    brightness *= kRefCount / static_cast<float>(std::max<size_t>(in.pointCount, 1)) * sizeRatio * sizeRatio;
  }
  program.set("uBrightness", brightness);
  program.set("uPeakTemperature", a.peakTemperature);
  program.set("uBeaming", a.dopplerBeaming);
  program.set("uColorShift", a.colorShift);
  program.set("uLifetime", in.lifetime);
  program.set("uFadeWithAge", a.fadeWithAge);
}

// Lensing of particles: every particle's light is deposited into a cube of
// N³ voxels around the hole; the ray tracer integrates it along bent rays.
bool Renderer::splatParticles(const FrameInput& in, const RenderSettings& settings) {
  const int res = std::clamp(settings.volumeResolution, 32, 512);
  if (res != volumeRes_) {
    glDeleteTextures(3, volumeSums_.data());
    glDeleteTextures(1, &volumeEmission_);
    glGenTextures(3, volumeSums_.data());
    for (GLuint tex : volumeSums_) {
      glBindTexture(GL_TEXTURE_3D, tex);
      glTexStorage3D(GL_TEXTURE_3D, 1, GL_R32UI, res, res, res);
    }
    glGenTextures(1, &volumeEmission_);
    glBindTexture(GL_TEXTURE_3D, volumeEmission_);
    glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA16F, res, res, res);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const float border[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glTexParameterfv(GL_TEXTURE_3D, GL_TEXTURE_BORDER_COLOR, border);
    for (GLenum axis : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
      glTexParameteri(GL_TEXTURE_3D, axis, GL_CLAMP_TO_BORDER);
    volumeRes_ = res;
  }
  const GLuint zero = 0;
  for (GLuint tex : volumeSums_) glClearTexImage(tex, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);

  const float extent = std::max(in.appearance.volumeExtent, 1.0f);
  constexpr float kFixedScale = 4194304.0f;  // 2^22: resolves 1e-7, overflows past ~1000 per voxel
  splatProgram_.use();
  setShadingUniforms(splatProgram_, in);
  splatProgram_.set("uCount", static_cast<unsigned>(in.pointCount));
  splatProgram_.set("uVolumeMin", glm::vec3(-0.5f * extent));
  splatProgram_.set("uVolumeSize", extent);
  splatProgram_.set("uResolution", res);
  splatProgram_.set("uFixedScale", kFixedScale);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pointsVbo_);
  for (int c = 0; c < 3; ++c) glBindImageTexture(static_cast<GLuint>(c), volumeSums_[static_cast<size_t>(c)], 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
  glDispatchCompute(static_cast<GLuint>((in.pointCount + 255) / 256), 1, 1);
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

  const float voxel = extent / static_cast<float>(res);
  volumeResolveProgram_.use();
  volumeResolveProgram_.set("uResolution", res);
  volumeResolveProgram_.set("uInvScaleVolume", settings.volumeGlow / (kFixedScale * voxel * voxel * voxel));
  for (int c = 0; c < 3; ++c) glBindImageTexture(static_cast<GLuint>(c), volumeSums_[static_cast<size_t>(c)], 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
  glBindImageTexture(3, volumeEmission_, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
  const GLuint groups = static_cast<GLuint>((res + 7) / 8);
  glDispatchCompute(groups, groups, groups);
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
  return true;
}

void Renderer::ensureTargets(int width, int height) {
  if (hdr_.width == width && hdr_.height == height) return;
  hdr_.create(width, height, GL_RGBA16F, true);
  for (auto& mip : bloomMips_) mip.destroy();
  bloomMips_.clear();
  int w = width, h = height;
  while (bloomMips_.size() < 10 && (bloomMips_.size() < 4 || std::min(w, h) / 2 >= 16)) {
    w = std::max(1, w / 2);
    h = std::max(1, h / 2);
    bloomMips_.emplace_back().create(w, h, GL_R11F_G11F_B10F, false);
  }
}

void Renderer::renderBloom(const RenderSettings& settings) {
  glDisable(GL_DEPTH_TEST);
  glBindVertexArray(emptyVao_);
  glActiveTexture(GL_TEXTURE0);

  // Downsample chain: hdr -> mip0 -> mip1 -> ...
  glDisable(GL_BLEND);
  bloomDownProgram_.use();
  bloomDownProgram_.set("uSource", 0);
  GLuint source = hdr_.color;
  int sw = hdr_.width, sh = hdr_.height;
  const int levels = static_cast<int>(bloomMips_.size());
  for (int i = 0; i < levels; ++i) {
    const gl::RenderTarget& dst = bloomMips_[static_cast<size_t>(i)];
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    glViewport(0, 0, dst.width, dst.height);
    glBindTexture(GL_TEXTURE_2D, source);
    bloomDownProgram_.set("uTexel", glm::vec2(1.0f / static_cast<float>(sw), 1.0f / static_cast<float>(sh)));
    bloomDownProgram_.set("uKaris", i == 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    source = dst.color;
    sw = dst.width;
    sh = dst.height;
  }

  // Upsample chain, accumulating into the larger mips: ... -> mip1 -> mip0.
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  bloomUpProgram_.use();
  bloomUpProgram_.set("uSource", 0);
  for (int i = levels - 1; i > 0; --i) {
    const gl::RenderTarget& src = bloomMips_[static_cast<size_t>(i)];
    const gl::RenderTarget& dst = bloomMips_[static_cast<size_t>(i - 1)];
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    glViewport(0, 0, dst.width, dst.height);
    glBindTexture(GL_TEXTURE_2D, src.color);
    bloomUpProgram_.set("uRadius", glm::vec2(settings.bloomRadius / static_cast<float>(src.width),
                                             settings.bloomRadius / static_cast<float>(src.height)));
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisable(GL_BLEND);
}

void Renderer::render(const FrameInput& in, const RenderSettings& settings, GLuint targetFbo) {
  ensureTargets(in.width, in.height);
  const glm::mat4 viewProj = in.projection * in.view;

  glBindFramebuffer(GL_FRAMEBUFFER, hdr_.fbo);
  glViewport(0, 0, in.width, in.height);
  glClearColor(0.0015f, 0.002f, 0.004f, 1.0f);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_PROGRAM_POINT_SIZE);

  // 0. Lensing of particles: splat them into the emission volume first.
  const int lensSource = in.appearance.lensSource;
  const bool volumeOn = settings.lensing && lensSource != Appearance::ThinDisk && in.pointCount > 0 &&
                        splatParticles(in, settings);
  glBindFramebuffer(GL_FRAMEBUFFER, hdr_.fbo);

  // 1. Background: the sky along straight rays, or the full ray-traced view
  //    (lensed sky, shadow, thin disk, particle volume) when lensing is on.
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  backgroundProgram_.use();
  backgroundProgram_.set("uInvViewProj", glm::inverse(viewProj));
  backgroundProgram_.set("uCameraPos", in.cameraPos);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, skyCubemap_);
  backgroundProgram_.set("uSky", 0);
  backgroundProgram_.set("uSkyBrightness", settings.stars ? settings.skyBrightness : 0.0f);
  backgroundProgram_.set("uLensing", settings.lensing);
  backgroundProgram_.set("uDisk", lensSource != Appearance::Particles);
  backgroundProgram_.set("uVolumeOn", volumeOn);
  if (volumeOn) {
    const float extent = std::max(in.appearance.volumeExtent, 1.0f);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, volumeEmission_);
    glActiveTexture(GL_TEXTURE0);
    backgroundProgram_.set("uVolume", 1);
    backgroundProgram_.set("uVolumeMin", glm::vec3(-0.5f * extent));
    backgroundProgram_.set("uVolumeSize", extent);
    backgroundProgram_.set("uVoxel", extent / static_cast<float>(volumeRes_));
  }
  const float isco = kerr::isco(settings.spin);
  backgroundProgram_.set("uDiskRadii", glm::vec2(settings.diskAtIsco ? isco : settings.diskInner, settings.diskOuter));
  backgroundProgram_.set("uSpin", settings.spin);
  backgroundProgram_.set("uHorizon", kerr::horizon(settings.spin));
  backgroundProgram_.set("uIsco", isco);
  // A smaller ISCO makes the hottest ring (6/r_isco)³ times brighter (same accretion
  // rate). Like a camera, partially expose for it; colors stay physical.
  const float exposureComp = settings.diskAutoExposure ? std::pow(isco / 6.0f, 1.5f) : 1.0f;
  backgroundProgram_.set("uDiskBrightness", settings.diskBrightness * exposureComp);
  backgroundProgram_.set("uPeakTemperature", settings.diskTemperature);
  backgroundProgram_.set("uBeaming", settings.diskBeaming);
  backgroundProgram_.set("uColorShift", settings.diskColorShift);
  backgroundProgram_.set("uTime", in.time);
  backgroundProgram_.set("uMaxSteps", settings.rayMaxSteps);
  backgroundProgram_.set("uStepScale", settings.rayStepScale);
  glBindVertexArray(emptyVao_);
  if (settings.lensing && settings.rayTileSize > 0) {
    glEnable(GL_SCISSOR_TEST);
    for (int y = 0; y < in.height; y += settings.rayTileSize) {
      for (int x = 0; x < in.width; x += settings.rayTileSize) {
        glScissor(x, y, settings.rayTileSize, settings.rayTileSize);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
      }
    }
    glDisable(GL_SCISSOR_TEST);
  } else {
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }

  // 2. Event horizon: writes depth so particles behind it are hidden. With
  //    lensing the ray tracer already drew the (larger) shadow: depth only.
  glEnable(GL_DEPTH_TEST);
  if (settings.lensing) glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  horizonProgram_.use();
  horizonProgram_.set("uInvViewProj", glm::inverse(viewProj));
  horizonProgram_.set("uViewProj", viewProj);
  horizonProgram_.set("uCameraPos", in.cameraPos);
  horizonProgram_.set("uRadius", gr::kHorizon);
  glBindVertexArray(emptyVao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  // 3. Guides.
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  linesProgram_.use();
  linesProgram_.set("uViewProj", viewProj);
  glBindVertexArray(linesVao_);
  auto drawLines = [&](const LineRange& r, GLenum mode, glm::vec4 color) {
    linesProgram_.set("uColor", color);
    glDrawArrays(mode, r.first, r.count);
  };
  if (settings.guideGrid) drawLines(grid_, GL_LINES, {0.55f, 0.65f, 1.0f, 0.06f});
  if (settings.guideHorizon) drawLines(horizonRing_, GL_LINE_LOOP, {1.0f, 1.0f, 1.0f, 0.35f});
  if (settings.guidePhotonSphere) drawLines(photonRing_, GL_LINE_LOOP, {1.0f, 0.72f, 0.35f, 0.45f});
  if (settings.guideIsco) drawLines(iscoRing_, GL_LINE_LOOP, {0.35f, 0.62f, 1.0f, 0.45f});

  // 4. Particles: additive Gaussian splats, depth-tested against the horizon.
  if (in.pointCount > 0 && (!settings.lensing || settings.particlesWhenLensing)) {
    glBlendFunc(GL_ONE, GL_ONE);
    pointsProgram_.use();
    pointsProgram_.set("uView", in.view);
    pointsProgram_.set("uProj", in.projection);
    pointsProgram_.set("uPixelsPerUnit",
                       static_cast<float>(in.height) / (2.0f * std::tan(in.fovYRadians * 0.5f)));
    setShadingUniforms(pointsProgram_, in);
    glBindVertexArray(pointsVao_);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(in.pointCount));
  }
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  // 5. Bloom + 6. tone-mapped composite into the window.
  if (settings.bloom) renderBloom(settings);
  glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
  glViewport(0, 0, in.width, in.height);
  glDisable(GL_DEPTH_TEST);
  compositeProgram_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, hdr_.color);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, settings.bloom ? bloomMips_[0].color : hdr_.color);
  compositeProgram_.set("uScene", 0);
  compositeProgram_.set("uBloom", 1);
  compositeProgram_.set("uBloomStrength", settings.bloom ? settings.bloomStrength : 0.0f);
  compositeProgram_.set("uExposure", settings.exposure);
  compositeProgram_.set("uVignette", settings.vignette);
  glBindVertexArray(emptyVao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  glBindVertexArray(0);
}

}  // namespace bh
