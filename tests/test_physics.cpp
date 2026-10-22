// Physics regression tests against analytic Schwarzschild results.
// Run: ctest --output-on-failure   (or ./bh_tests)
#include <cmath>
#include <cstdio>

#include "backend/CpuBackend.hpp"
#include "core/Kerr.hpp"
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

}  // namespace

int main() {
  testCircularOrbit();
  testRadialFreeFall();
  testPhotonCapture();
  testWeakDeflection();
  testRedshift();
  testKerr();
  testBackendSmoke();
  std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
  return g_failures ? 1 : 0;
}
