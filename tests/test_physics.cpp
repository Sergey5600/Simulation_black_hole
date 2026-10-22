// Physics regression tests against analytic Schwarzschild results.
// Run: ctest --output-on-failure   (or ./bh_tests)
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <vector>

#include "io/VdbWriter.hpp"
#include "miniz.h"

#include "backend/CpuBackend.hpp"
#include "core/Fall.hpp"
#include "core/Kerr.hpp"
#include "core/Spectrum.hpp"
#include "sim/Journey.hpp"
#include "sim/Integrator.hpp"
#include "sim/Spawn.hpp"

using namespace bh;

namespace {

constexpr double kPi = 3.14159265358979323846;
int g_failures = 0;

void check(bool ok, const char* what, double got, double want) {
  std::printf("[%s] %-58s got %.6g, want %.6g\n", ok ? " OK " : "FAIL", what, got, want);
  if (!ok) ++g_failures;
}

void checkNear(const char* what, double got, double want, double relTol) {
  check(std::fabs(got - want) <= relTol * std::fabs(want), what, got, want);
}

Particle massive(Vec3 x, Vec3 v) { return Particle{x, 0.0f, v, kAlive, 1.0f, 0.0f}; }
Particle photon(Vec3 x, Vec3 v) { return Particle{x, 0.0f, v, kAlive | kPhoton, 0.0f, 0.0f}; }

StepParams properTime(float dt, float accuracy = 0.01f) {
  StepParams sp;
  sp.dt = dt;
  sp.timeMode = TimeMode::Proper;
  sp.accuracy = accuracy;
  sp.maxSubsteps = 1 << 20;
  sp.outerRadius = 1e6f;
  return sp;
}

void testCircularOrbit() {
  const float r = 10.0f;
  Particle p = massive({r, 0, 0}, {0, 0, -gr::circularSpeed(r)});
  checkNear("circular orbit: E = (1-2/r)/sqrt(1-3/r)", gr::energy(p.x, p.v, 1.0f),
            (1.0 - 2.0 / r) / std::sqrt(1.0 - 3.0 / r), 1e-5);

  // Proper period of a circular orbit: 2π r^{3/2} sqrt(1 - 3M/r) / sqrt(M).
  const double period = 2.0 * kPi * std::pow(r, 1.5) * std::sqrt(1.0 - 3.0 / r);
  float rMin = r, rMax = r;
  for (int i = 0; i < 1000; ++i) {
    advanceParticle(p, properTime(static_cast<float>(5.0 * period / 1000.0)));
    const float rr = length(p.x);
    rMin = std::fmin(rMin, rr);
    rMax = std::fmax(rMax, rr);
  }
  checkNear("circular orbit r=10 stays circular over 5 orbits (min r)", rMin, r, 1e-3);
  checkNear("circular orbit r=10 stays circular over 5 orbits (max r)", rMax, r, 1e-3);
  checkNear("circular orbit returns to start after 5 proper periods (x)", p.x.x, r, 2e-3);
}

void testRadialFreeFall() {
  // Drop from rest at r0: cycloid r = r0/2 (1 + cos η), τ = sqrt(r0³/8M) (η + sin η).
  const float r0 = 10.0f;
  const double eta = kPi / 2.0;
  const double tau = std::sqrt(r0 * r0 * r0 / 8.0) * (eta + std::sin(eta));
  Particle p = massive({r0, 0, 0}, {0, 0, 0});
  advanceParticle(p, properTime(static_cast<float>(tau)));
  checkNear("free fall from rest: r(τ) matches cycloid", length(p.x), r0 / 2.0, 1e-3);

  // Proper time: it crosses the horizon (and is removed) before τ(η=π).
  const double tauEnd = std::sqrt(r0 * r0 * r0 / 8.0) * kPi;
  const bool alive = advanceParticle(p, properTime(static_cast<float>(tauEnd - tau)));
  check(!alive, "free fall in proper time crosses the horizon", alive, 0);

  // Coordinate time: a distant observer never sees it cross; it only gets removed
  // once 1 - 2M/r < horizonLapse, i.e. just outside r = 2M.
  Particle q = massive({r0, 0, 0}, {0, 0, 0});
  StepParams sp = properTime(1000.0f);
  sp.timeMode = TimeMode::Coordinate;
  sp.horizonLapse = 1e-3f;
  for (int i = 0; i < 400 && advanceParticle(q, sp); ++i) {
  }
  const float rEnd = length(q.x);
  check(!isAlive(q) && rEnd > 2.0f && rEnd < 2.01f, "coordinate-time infall freezes just outside r = 2M", rEnd, 2.0);
}

bool photonCaptured(float b) {
  Particle p = photon({-200.0f, 0, b}, {1, 0, 0});
  StepParams sp = properTime(1000.0f, 0.002f);
  sp.outerRadius = 400.0f;
  advanceParticle(p, sp);
  return length(p.x) < 100.0f;  // removed at the horizon (escaping photons fly off)
}

void testPhotonCapture() {
  const float bc = gr::kCriticalImpact;
  check(photonCaptured(bc * 0.995f), "photon with b = 0.995 b_crit is captured", 1, 1);
  check(!photonCaptured(bc * 1.005f), "photon with b = 1.005 b_crit escapes", 0, 0);
}

void testWeakDeflection() {
  // Deflection to second order: α = 4M/b + 15πM²/(4b²).
  const float b = 100.0f;
  Particle p = photon({-4000.0f, 0, b}, {1, 0, 0});
  StepParams sp = properTime(8000.0f, 0.002f);
  advanceParticle(p, sp);
  const double alpha = std::atan2(-p.v.z, p.v.x);
  checkNear("weak-field deflection 4M/b + 15πM²/4b² (b = 100 M)", alpha, 4.0 / b + 15.0 * kPi / (4.0 * b * b),
            0.01);
}

void testRedshift() {
  // Transverse emission from a circular orbit: g = sqrt(1 - 3M/r).
  const float r = 8.0f;
  const Vec3 x{r, 0, 0}, v{0, 0, -gr::circularSpeed(r)};
  const gr::LocalKinematics k = gr::localKinematics(x, v);
  checkNear("circular orbit, transverse emission: g = sqrt(1-3M/r)", gr::redshiftFactor(k, {0, 1, 0}),
            std::sqrt(1.0 - 3.0 / r), 1e-5);
  // Static observer frame speed of a circular orbit: β = sqrt(M / (r - 2M)).
  checkNear("circular orbit local speed beta = sqrt(M/(r-2M))", length(k.beta), std::sqrt(1.0 / (r - 2.0)), 1e-5);
}

// Kerr: equatorial photon with impact parameter b = L_z / E, prograde if b > 0.
// Returns true if it falls into the hole.
bool kerrPhotonCaptured(float a, float b) {
  const float R = 300.0f;
  // Formula axes: spin along +Z, photon moves along +X in the plane Z = 0.
  // L_z = X p_Y - Y p_X = -Y k, so start at Y = -b/k for L_z = b.
  Vec3 X{-R, 0.0f, 0.0f};
  Vec3 p = kerr::nullMomentum(X, {1, 0, 0}, a);
  X.y = -b / p.x;
  p = kerr::nullMomentum(X, {1, 0, 0}, a);
  const float rh = kerr::horizon(a);
  for (int i = 0; i < 400000; ++i) {
    const float r = kerr::radius(X, a);
    if (r < rh * 1.01f) return true;
    if (r > 2.0f * R) return false;
    const float h = 0.002f * r;
    Vec3 dx1, dp1, dx2, dp2, dx3, dp3, dx4, dp4;
    kerr::derivatives(X, p, 1.0f, a, dx1, dp1);
    kerr::derivatives(X + dx1 * (0.5f * h), p + dp1 * (0.5f * h), 1.0f, a, dx2, dp2);
    kerr::derivatives(X + dx2 * (0.5f * h), p + dp2 * (0.5f * h), 1.0f, a, dx3, dp3);
    kerr::derivatives(X + dx3 * h, p + dp3 * h, 1.0f, a, dx4, dp4);
    X += (dx1 + dx2 * 2.0f + dx3 * 2.0f + dx4) * (h / 6.0f);
    p += (dp1 + dp2 * 2.0f + dp3 * 2.0f + dp4) * (h / 6.0f);
  }
  return false;
}

void testKerr() {
  checkNear("Kerr ISCO a = 0 is 6M", kerr::isco(0.0f), 6.0, 1e-5);
  checkNear("Kerr ISCO a = 0.998 (prograde) is 1.237M", kerr::isco(0.998f), 1.2370, 2e-3);
  checkNear("Kerr ISCO a = -1 (retrograde) is 9M", kerr::isco(-1.0f), 9.0, 1e-4);
  checkNear("Kerr horizon a = 0.6 is 1.8M", kerr::horizon(0.6f), 1.8, 1e-6);

  // Critical equatorial impact parameters: b = ∓a + 6M cos(arccos(±a)/3).
  struct Case {
    float a, b;
    const char* name;
  };
  const Case cases[] = {
      {0.0f, 5.196152f, "Kerr a=0: photon capture at b = 3*sqrt(3)"},
      {0.9f, 2.844782f, "Kerr a=0.9 prograde photon capture at b = 2.845"},
      {0.9f, -6.832316f, "Kerr a=0.9 retrograde photon capture at b = -6.832"},
  };
  for (const Case& c : cases) {
    const bool inner = kerrPhotonCaptured(c.a, c.b * 0.99f);
    const bool outer = kerrPhotonCaptured(c.a, c.b * 1.01f);
    check(inner && !outer, c.name, inner ? (outer ? 2 : 1) : 0, 1);
  }
}

// Kerr particles: spawn a state in the Kerr representation.
Particle kerrParticle(Vec3 X, Vec3 P, float a, bool photon) {
  Particle q{};
  q.x = kerr::fromKS(X);
  q.v = kerr::fromKS(P);
  q.flags = kAlive | (photon ? kPhoton : 0u);
  q.energy = kerr::energyFor(X, P, a, photon ? 0.0f : 1.0f);
  return q;
}

Particle kerrCircular(float r, float a) {
  float ut = 1.0f, omega = 0.0f;
  kerr::circularOrbit(r, a, ut, omega);
  const Vec3 X{r, -a, 0.0f};  // KS azimuth φ = 0
  const Vec3 u = Vec3{-X.y, X.x, 0.0f} * (omega * ut);
  return kerrParticle(X, kerr::covariantMomentum(X, u, ut, a), a, false);
}

void testKerrParticles() {
  // The mass shell reproduces the Bardeen-Press-Teukolsky circular-orbit energy.
  {
    const float a = 0.9f, r = 4.0f;
    // BPT: E = (r^1.5 - 2 r^0.5 + a) / (r^0.75 sqrt(r^1.5 - 3 r^0.5 + 2a)).
    const double sr = std::sqrt(r);
    const double e = (r * sr - 2.0 * sr + a) / (std::pow(r, 0.75) * std::sqrt(r * sr - 3.0 * sr + 2.0 * a));
    const Particle q = kerrCircular(r, a);
    checkNear("Kerr a=0.9 circular orbit r=4: energy from mass shell = BPT E", q.energy, e, 1e-4);
  }
  // A circular orbit stays circular (Boyer-Lindquist r) over 3 orbits.
  {
    const float a = 0.9f, r = 4.0f;
    Particle q = kerrCircular(r, a);
    StepParams sp = properTime(1.0f, 0.005f);
    sp.spin = a;
    float rMin = r, rMax = r;
    for (int i = 0; i < 150; ++i) {
      advanceParticle(q, sp);
      const float rr = kerr::radius(kerr::toKS(q.x), a);
      rMin = std::fmin(rMin, rr);
      rMax = std::fmax(rMax, rr);
    }
    checkNear("Kerr a=0.9 prograde circular orbit r=4 stays circular (min r)", rMin, r, 2e-3);
    checkNear("Kerr a=0.9 prograde circular orbit r=4 stays circular (max r)", rMax, r, 2e-3);
  }
  // Tiny spin reproduces the Schwarzschild proper orbital period.
  {
    const float a = 1e-3f, r = 10.0f;
    Particle q = kerrCircular(r, a);
    const Vec3 start = q.x;
    const double period = 2.0 * kPi * std::pow(r, 1.5) * std::sqrt(1.0 - 3.0 / r);
    StepParams sp = properTime(static_cast<float>(period / 200.0), 0.005f);
    sp.spin = a;
    for (int i = 0; i < 200; ++i) advanceParticle(q, sp);
    check(length(q.x - start) < 0.05f, "Kerr a=0.001 orbit returns after the Schwarzschild period", length(q.x - start),
          0.0);
  }
  // Observer time: infall freezes just outside r+.
  {
    const float a = 0.9f;
    Particle q = kerrParticle({10.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, a, false);
    StepParams sp = properTime(1000.0f);
    sp.spin = a;
    sp.timeMode = TimeMode::Coordinate;
    sp.horizonLapse = 0.01f;
    for (int i = 0; i < 400 && advanceParticle(q, sp); ++i) {
    }
    const float rEnd = kerr::radius(kerr::toKS(q.x), a);
    const float rh = kerr::horizon(a);
    check(!isAlive(q) && rEnd > rh && rEnd < rh * 1.03f, "Kerr a=0.9 observer-time infall freezes just outside r+", rEnd,
          rh);
  }
  // Photons through the particle integrator: prograde capture threshold b = 2.845.
  {
    const float a = 0.9f, bc = 2.844782f;
    auto captured = [&](float b) {
      const float R = 300.0f;
      Vec3 X{-R, 0.0f, 0.0f};
      Vec3 P = kerr::nullMomentum(X, {1, 0, 0}, a);
      X.y = -b / P.x;
      P = kerr::nullMomentum(X, {1, 0, 0}, a);
      Particle q = kerrParticle(X, P, a, true);
      q.energy = 1.0f;
      StepParams sp = properTime(2000.0f, 0.002f);
      sp.spin = a;
      sp.outerRadius = 2.0f * R;
      advanceParticle(q, sp);
      return kerr::radius(kerr::toKS(q.x), a) < 10.0f;
    };
    const bool in = captured(bc * 0.99f), out = captured(bc * 1.01f);
    check(in && !out, "Kerr particle integrator: prograde photon capture at b = 2.845", in ? (out ? 2 : 1) : 0, 1);
  }
  // Every scenario spawns finite Kerr states and survives a few steps.
  for (ScenarioType type : {ScenarioType::AccretionDisk, ScenarioType::PhotonWaves, ScenarioType::StarDisruption}) {
    CpuBackend backend;
    ScenarioParams sc;
    sc.type = type;
    sc.spin = 0.7f;
    sc.respawnDead = type == ScenarioType::AccretionDisk;
    sc.star.center = {-60.0f, 0.0f, 0.0f};
    sc.star.velocity = {0.12f, 0.0f, 0.03f};
    backend.resize(5000);
    backend.spawn(sc, 0, backend.size(), 3u, 0.0f, true);
    StepParams sp;
    sp.dt = 1.0f;
    sp.spin = sc.spin;
    for (int i = 0; i < 20; ++i) backend.step(sc, sp, 9u + static_cast<uint32_t>(i), static_cast<float>(i));
    std::vector<PointVertex> verts(backend.size());
    backend.writeVertices(verts.data());
    size_t finite = 0;
    for (const PointVertex& pv : verts) {
      finite += std::isfinite(pv.pos[0]) && std::isfinite(pv.dir[0]) && std::isfinite(pv.value);
    }
    check(finite == verts.size() && backend.stats().alive > 0, "Kerr a=0.7 scenario spawns and steps finite states",
          static_cast<double>(finite), static_cast<double>(verts.size()));
  }
}

void testBackendSmoke() {
  CpuBackend backend;
  ScenarioParams sc;
  sc.type = ScenarioType::AccretionDisk;
  sc.respawnDead = true;
  backend.resize(20000);
  backend.spawn(sc, 0, backend.size(), 42u, 0.0f, true);
  StepParams sp;
  sp.dt = 1.0f;
  for (int i = 0; i < 50; ++i) backend.step(sc, sp, 7u + static_cast<uint32_t>(i), static_cast<float>(i));
  std::vector<PointVertex> verts(backend.size());
  backend.writeVertices(verts.data());
  size_t finite = 0, alive = 0;
  for (const PointVertex& pv : verts) {
    finite += std::isfinite(pv.pos[0]) && std::isfinite(pv.pos[1]) && std::isfinite(pv.pos[2]);
    alive += pv.value >= 0.0f;
  }
  check(finite == verts.size(), "disk backend: every vertex position finite after 50 steps",
        static_cast<double>(finite), static_cast<double>(verts.size()));
  // Slots that die during a step are refilled on the next one.
  check(alive == backend.stats().alive, "disk backend: alive vertices match backend stats",
        static_cast<double>(alive), static_cast<double>(backend.stats().alive));
  check(alive > verts.size() * 99 / 100, "disk backend: >99% of slots alive (continuous refill)",
        static_cast<double>(alive), static_cast<double>(verts.size()));
}

void testSpectrum() {
  using namespace spectrum;
  checkNear("black body: 6500 K has luminance 1 (normalization)", luminance(planckRgb(6500.0)), 1.0, 1e-9);
  const Rgb white = planckColor(6500.0), red = planckColor(3000.0), blue = planckColor(20000.0);
  check(std::fmin(white.g, white.b) > 0.9, "black body: 6500 K is white (min channel)", std::fmin(white.g, white.b), 0.9);
  check(red.r == 1.0 && red.b < 0.25, "black body: 3000 K is orange-red (blue channel)", red.b, 0.25);
  check(blue.b == 1.0 && blue.r < 0.5, "black body: 20000 K is blue (red channel)", blue.r, 0.5);
  // Seen 14x blueshifted (hovering at 2.01 M), 6500 K light is a 91000 K black
  // body: ~166x brighter in the visible band, far from the bolometric 14⁴ = 38416.
  checkNear("black body: visible brightening of 6500 K blueshifted to 91000 K",
            luminance(planckRgb(91000.0)) / luminance(planckRgb(6500.0)), 165.7, 0.02);
  checkNear("black body: Rayleigh-Jeans tail, L(10⁶ K) / L(5·10⁵ K) -> 2",
            luminance(planckRgb(1e6)) / luminance(planckRgb(5e5)), 2.0, 0.02);
  // The shader table spans the temperatures it is asked for.
  checkNear("black body: lookup table ends at 100 K and 10⁶ K", lutTemperature(0) * lutTemperature(kLutSize - 1), 1e8, 1e-9);
}

// Backward ray from an observer at radius r looking along local direction
// (dr outward, dPerp sideways), with the same frame formulas as lensing.frag.glsl:
//   static: path ∝ r̂ sqrt(f) dr + d⊥;  falling with energy E: path ∝ r̂ (w + E dr) + d⊥.
bool rayEscapes(double r, bool falling, double energy, double dr) {
  const double dPerp = std::sqrt(1.0 - dr * dr);
  const double f = 1.0 - 2.0 / r;
  const double radial = falling ? std::sqrt(energy * energy - f) + energy * dr : std::sqrt(f) * dr;
  Vec3 x{static_cast<float>(r), 0.0f, 0.0f};
  Vec3 v = normalize(Vec3{static_cast<float>(radial), static_cast<float>(dPerp), 0.0f});
  const float h2 = dot(cross(x, v), cross(x, v));
  auto accel = [h2](Vec3 p) {
    const float r2 = dot(p, p);
    return p * (-3.0f * h2 / (r2 * r2 * std::sqrt(r2)));
  };
  for (int i = 0; i < 400000; ++i) {
    const float rr = length(x);
    if (rr < 2.0f && dot(x, v) < 0.0f) return false;
    if (rr > 2000.0f && dot(x, v) > 0.0f) return true;
    const float h = 0.002f * rr;
    const Vec3 k1x = v, k1v = accel(x);
    const Vec3 k2x = v + k1v * (0.5f * h), k2v = accel(x + k1x * (0.5f * h));
    const Vec3 k3x = v + k2v * (0.5f * h), k3v = accel(x + k2x * (0.5f * h));
    const Vec3 k4x = v + k3v * h, k4v = accel(x + k3x * h);
    x = x + (k1x + k2x * 2.0f + k3x * 2.0f + k4x) * (h / 6.0f);
    v = v + (k1v + k2v * 2.0f + k3v * 2.0f + k4v) * (h / 6.0f);
  }
  return false;
}

void testFall() {
  checkNear("fall: proper time from the horizon to r = 0, from rest at infinity = 4/3 M",
            fall::properTimeToSingularity(2.0, 1.0), 4.0 / 3.0, 1e-6);
  checkNear("fall: dropped from rest at 10 M, proper time to r = 0 = π (r0/2M)^1.5 M",
            fall::properTimeToSingularity(10.0, fall::energyFromRest(10.0)), kPi * std::pow(5.0, 1.5), 1e-4);
  checkNear("fall: hovering at 2.5 M, the sky is a circle of 68.4° radius",
            fall::staticSkyRadius(2.5) * 180.0 / kPi, 68.38, 1e-3);
  checkNear("fall: hovering at 2.01 M, the sky is a circle of 10.5° radius",
            fall::staticSkyRadius(2.01) * 180.0 / kPi, 10.50, 2e-3);
  checkNear("fall: at the photon sphere the sky is exactly half the view", fall::staticSkyRadius(3.0), kPi / 2, 1e-6);
  checkNear("fall: hovering at 2.01 M, outside clocks run 14.2x faster", fall::overheadShiftStatic(2.01), 14.177, 1e-3);
  checkNear("fall: hover thrust at 4 M = M / (r² sqrt(1-2M/r))", fall::hoverAcceleration(4.0), 1.0 / (16.0 * std::sqrt(0.5)), 1e-9);
  checkNear("fall: Sagittarius A* (4.3e6 suns) has M = 21.2 s", fall::Units(4.3e6).time, 21.18, 1e-3);

  // The ray tracer's observer frames put the edge of the sky where the critical
  // impact parameter b = 3√3 M says: b = r d⊥ / sqrt(f) (static),
  // b = r d⊥ / (E + w dr) (falling). Rays just inside the edge escape.
  const double bc = 3.0 * std::sqrt(3.0);
  {
    const double r = 2.5, psi = fall::staticSkyRadius(r);
    check(rayEscapes(r, false, 0.0, std::cos(psi * 0.98)) && !rayEscapes(r, false, 0.0, std::cos(psi * 1.02)),
          "fall: static observer at 2.5 M, rays escape just inside the sky edge", 1.0, 1.0);
  }
  for (double r : {2.5, 1.2}) {  // falling from rest at infinity, outside and inside the horizon
    const double e = 1.0, w = fall::infallRate(r, e);
    double lo = 0.0, hi = kPi;  // angle from outward with b(angle) = bc
    for (int i = 0; i < 60; ++i) {
      const double mid = 0.5 * (lo + hi);
      const double b = r * std::sin(mid) / (e + w * std::cos(mid));
      (b < bc ? lo : hi) = mid;
    }
    char what[96];
    std::snprintf(what, sizeof(what), "fall: rain observer at %.1f M, rays escape just inside the sky edge", r);
    check(rayEscapes(r, true, e, std::cos(lo * 0.98)) && !rayEscapes(r, true, e, std::cos(lo * 1.02)), what, 1.0, 1.0);
  }
}

void testJourney() {
  Journey j;
  check(Journey::named("infall", 40.0f, j), "journey: infall exists", 1.0, 1.0);
  const double tauInfall = j.at(1.0f).properTime - j.at(0.0f).properTime;
  checkNear("journey: infall from 40 M to 0.5 M takes (2/3)(r0^1.5 - r1^1.5)/sqrt(2M) of proper time", tauInfall,
            2.0 / 3.0 * (std::pow(40.0, 1.5) - std::pow(0.5, 1.5)) / std::sqrt(2.0), 1e-3);

  check(Journey::named("hover", 40.0f, j), "journey: hover exists", 1.0, 1.0);
  const JourneyFrame mid = j.at(0.5f), end = j.at(1.0f);
  check(mid.hovering && std::fabs(mid.r - 2.01f) < 1e-4f && std::fabs(mid.gaze - kPi) < 1e-3,
        "journey: hover at 2.01 M looking straight up", mid.r, 2.01);
  const JourneyFrame a = j.at(0.45f), b = j.at(0.55f);
  checkNear("journey: while hovering, outside time runs 14.2x faster",
            (b.outsideTime - a.outsideTime) / (b.properTime - a.properTime), 14.177, 1e-3);
  check(!end.hovering && end.r < 0.3f, "journey: hover ends falling deep inside the horizon", end.r, 0.25);
  // Release at u = 0.62: compare the drop's duration with the closed-form fall time.
  const double e = fall::energyFromRest(2.01);
  const JourneyFrame release = j.at(0.62f);
  checkNear("journey: drop from 2.01 M to 0.25 M matches the analytic proper time", end.properTime - release.properTime,
            fall::properTimeToSingularity(2.01, e) - fall::properTimeToSingularity(0.25, e), 2e-3);
  bool monotonic = true;
  for (int i = 1; i <= 1000; ++i) {
    const JourneyFrame p = j.at((i - 1) / 1000.0f), q = j.at(i / 1000.0f);
    monotonic = monotonic && q.properTime >= p.properTime && q.outsideTime >= p.outsideTime && q.r <= p.r + 1e-6f;
  }
  check(monotonic, "journey: clocks run forward and r never grows", 1.0, 1.0);

  // The peach story: tides tear a 7.6 cm peach (flesh ~1000 kg/m³, ~0.1 MPa)
  // once 2GM/R³ > 4σ/(ρℓ²). Near r = 0 (r ∝ δτ^2/3) the deviation equations
  // have power-law solutions; starting from rest at r_t they give
  // stretch = 4/5 (r_t/r)^1/2 + 1/5 (r/r_t)², squeeze = 2 (r/r_t)^1/2 - r/r_t.
  check(Journey::named("peach", 40.0f, j, 4.3e6), "journey: peach exists", 1.0, 1.0);
  const fall::Units units(4.3e6);
  const double tearRate = 4.0 * 1e5 / (1000.0 * 0.076 * 0.076);
  const double tearRadius = std::cbrt(2.0 * units.accel / units.length / tearRate);
  double firstTorn = 0.0;
  for (int i = 0; i <= 4000 && firstTorn == 0.0; ++i) {
    const JourneyFrame f = j.at(i / 4000.0f);
    if (f.tornApart) firstTorn = f.r;
  }
  checkNear("journey: the peach tears where the tides beat the strength of its flesh", firstTorn, tearRadius, 0.03);
  const JourneyFrame last = j.at(1.0f);
  const double x = last.r / firstTorn;
  checkNear("journey: torn peach stretches as 4/5 (r_t/r)^1/2 + 1/5 (r/r_t)^2", last.peachStretch,
            0.8 / std::sqrt(x) + 0.2 * x * x, 0.01);
  checkNear("journey: and is squeezed as 2 (r/r_t)^1/2 - r/r_t", last.peachSqueeze, 2.0 * std::sqrt(x) - x, 0.01);
  const JourneyFrame hover = j.at(0.5f);
  check(hover.hovering && std::fabs(length(hover.peachOffset) - 0.47f) < 0.02f && hover.cosmicTime > 0.2f &&
            hover.cosmicTime < 1.0f,
        "journey: while hovering the peach is held at the lens and the outside clock runs", length(hover.peachOffset), 0.47);
  checkNear("journey: the story clock reaches 1 at the release", j.at(0.58f).cosmicTime, 1.0, 1e-3);
}

// Reads back what vdb::writeFile writes (the OpenVDB 10 layout, no tiles) into
// a map from voxel coordinates to values. Checked against OpenVDB itself once;
// this keeps the writer honest without depending on the library.
struct VdbReadBack {
  std::vector<std::string> names;
  std::map<std::tuple<int, int, int>, std::vector<float>> voxels[2];
  bool ok = true;
};

VdbReadBack readVdb(const std::string& path) {
  VdbReadBack out;
  std::ifstream f(path, std::ios::binary);
  const std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  size_t at = 0;
  auto get = [&](void* p, size_t n) {
    if (at + n > d.size()) { out.ok = false; return; }
    std::memcpy(p, d.data() + at, n);
    at += n;
  };
  auto u32 = [&] { uint32_t v = 0; get(&v, 4); return v; };
  auto i64 = [&] { int64_t v = 0; get(&v, 8); return v; };
  auto str = [&] { std::string s(u32(), '\0'); get(s.data(), s.size()); return s; };
  if (i64() != 0x56444220 || u32() != 224) out.ok = false;
  at += 4 + 4 + 1 + 36;
  if (u32() != 0) out.ok = false;
  const uint32_t gridCount = u32();
  for (uint32_t g = 0; g < gridCount && g < 2 && out.ok; ++g) {
    out.names.push_back(str());
    const int components = str() == "Tree_vec3s_5_4_3" ? 3 : 1;
    str();
    const int64_t gridPos = i64(), blockPos = i64(), endPos = i64();
    at = static_cast<size_t>(gridPos);
    if (u32() != 3) out.ok = false;  // ZIP | active mask
    for (uint32_t m = u32(); m > 0; --m) {
      str();
      str();
      at += u32();
    }
    str();
    double map[18];
    get(map, sizeof(map));
    at += 4 + 4 * components;  // buffer count, background
    if (u32() != 0) out.ok = false;
    struct LeafInfo { int x, y, z; uint64_t mask[8]; };
    std::vector<LeafInfo> leaves;
    auto skipValues = [&] { at += 1; const int64_t n = i64(); at += static_cast<size_t>(n < 0 ? -n : n); };
    for (uint32_t c = u32(); c > 0 && out.ok; --c) {
      int top[3];
      get(top, 12);
      uint64_t mask32[512];
      get(mask32, sizeof(mask32));
      at += 4096;
      skipValues();
      for (int a = 0; a < 32768; ++a) {
        if (!(mask32[a >> 6] >> (a & 63) & 1)) continue;
        const int ox = top[0] + ((a >> 10) << 7), oy = top[1] + (((a >> 5) & 31) << 7), oz = top[2] + ((a & 31) << 7);
        uint64_t mask16[64];
        get(mask16, sizeof(mask16));
        at += 512;
        skipValues();
        for (int b = 0; b < 4096; ++b) {
          if (!(mask16[b >> 6] >> (b & 63) & 1)) continue;
          LeafInfo leaf{ox + ((b >> 8) << 3), oy + (((b >> 4) & 15) << 3), oz + ((b & 15) << 3), {}};
          get(leaf.mask, 64);
          leaves.push_back(leaf);
        }
      }
    }
    if (static_cast<int64_t>(at) != blockPos) out.ok = false;
    for (const LeafInfo& leaf : leaves) {
      uint64_t mask[8];
      get(mask, 64);
      at += 1;
      const int64_t size = i64();
      int active = 0;
      for (uint64_t w : mask) active += std::popcount(w);
      std::vector<float> values(static_cast<size_t>(active * components));
      if (size < 0) {
        get(values.data(), static_cast<size_t>(-size));
      } else {
        mz_ulong length = static_cast<mz_ulong>(values.size() * 4);
        if (mz_uncompress(reinterpret_cast<unsigned char*>(values.data()), &length,
                          reinterpret_cast<const unsigned char*>(d.data() + at), static_cast<mz_ulong>(size)) != MZ_OK)
          out.ok = false;
        at += static_cast<size_t>(size);
      }
      int k = 0;
      for (int n = 0; n < 512; ++n) {
        if (!(mask[n >> 6] >> (n & 63) & 1)) continue;
        out.voxels[g][{leaf.x + (n >> 6), leaf.y + ((n >> 3) & 7), leaf.z + (n & 7)}] =
            std::vector<float>(values.begin() + k * components, values.begin() + (k + 1) * components);
        ++k;
      }
    }
    if (static_cast<int64_t>(at) != endPos) out.ok = false;  // the next grid's descriptor follows
  }
  return out;
}

void testVdbWriter() {
  // Sparse random data across node boundaries and negative coordinates.
  const std::array<int, 3> dims{21, 13, 140}, origin{-11, -4100, 4000};
  const size_t n = static_cast<size_t>(dims[0]) * dims[1] * dims[2];
  vdb::Grid density{"density", 1, std::vector<float>(n)}, color{"color", 3, std::vector<float>(n * 3)};
  std::mt19937 rng(3);
  std::uniform_real_distribution<float> u(0.0f, 1.0f);
  size_t filled = 0;
  for (size_t i = 0; i < n; ++i) {
    if (u(rng) > 0.05f) continue;
    density.values[i] = 1.0f + u(rng);
    color.values[3 * i] = u(rng);
    color.values[3 * i + 2] = 0.5f;
    ++filled;
  }
  const std::string path = (std::filesystem::temp_directory_path() / "bh_test.vdb").string();
  vdb::Transform transform;
  transform.voxelSize = 0.5;
  check(vdb::writeFile(path, {density, color}, dims, origin, transform), "vdb: file written", 1.0, 1.0);
  const VdbReadBack back = readVdb(path);
  bool same = back.ok && back.names.size() == 2 && back.names[0] == "density" && back.names[1] == "color" &&
              back.voxels[0].size() == filled && back.voxels[1].size() == filled;
  for (int i = 0; same && i < dims[0]; ++i)
    for (int j = 0; same && j < dims[1]; ++j)
      for (int k = 0; same && k < dims[2]; ++k) {
        const size_t idx = (static_cast<size_t>(i) * dims[1] + j) * dims[2] + k;
        if (density.values[idx] == 0.0f) continue;
        const auto key = std::make_tuple(origin[0] + i, origin[1] + j, origin[2] + k);
        const auto d = back.voxels[0].find(key);
        const auto c = back.voxels[1].find(key);
        same = d != back.voxels[0].end() && c != back.voxels[1].end() && d->second[0] == density.values[idx] &&
               c->second[0] == color.values[3 * idx] && c->second[2] == color.values[3 * idx + 2];
      }
  check(same, "vdb: every active voxel reads back (float + vec3, ZIP, negative coords)", static_cast<double>(back.voxels[0].size()),
        static_cast<double>(filled));
  std::filesystem::remove(path);
}

}  // namespace

int main() {
  testCircularOrbit();
  testRadialFreeFall();
  testPhotonCapture();
  testWeakDeflection();
  testRedshift();
  testKerr();
  testKerrParticles();
  testBackendSmoke();
  testSpectrum();
  testFall();
  testJourney();
  testVdbWriter();
  std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
  return g_failures ? 1 : 0;
}
