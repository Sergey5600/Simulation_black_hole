// Background + gravitational lensing. Without lensing: the sky along straight
// camera rays. With lensing: every pixel traces a photon geodesic backwards
// from the camera, until the ray
//   * falls through the horizon      -> black (the shadow),
//   * crosses the thin disk          -> Novikov-Thorne emission with the exact redshift,
//   * escapes                        -> the sky in its asymptotic direction.
// Spin 0 uses the exact Schwarzschild null equation in pseudo-Cartesian form,
// x'' = -3M h² x / r⁵ (h = |x × x'|). Spin ≠ 0 integrates Hamilton's equations
// in Cartesian Kerr-Schild coordinates (same formulas as src/core/Kerr.hpp).
in vec2 vUv;
out vec4 fragColor;

uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform samplerCube uSky;
uniform float uSkyBrightness;
uniform bool uLensing;

uniform bool uDisk;

// Particle light splatted into a 3D emission grid (Renderer::splatParticles).
uniform bool uVolumeOn;
uniform sampler3D uVolume;
uniform vec3 uVolumeMin;
uniform float uVolumeSize;
uniform float uVoxel;
uniform vec2 uDiskRadii;       // inner, outer (M)
uniform float uDiskBrightness;
uniform float uPeakTemperature;
uniform bool uBeaming;
uniform bool uColorShift;
uniform float uTime;           // drives the differential rotation of the disk texture

uniform int uMaxSteps;
uniform float uStepScale;      // affine step as a fraction of r

uniform float uSpin;           // a/M, along +y (the disk's rotation axis)
uniform float uHorizon;        // r+ = 1 + sqrt(1 - a²)
uniform float uIsco;           // sets the temperature profile

// Camera frame. false: a static observer at uCameraPos. true: an observer in
// radial free fall from rest at infinity (the Painleve-Gullstrand "rain" frame),
// which works inside the horizon too.
uniform bool uRainFrame;

// Frequency ratio ν_camera / E for the current pixel's photon (set in main()).
float gCamera = 1.0;

// Light from the sky or the particles as seen by a moving or deep camera:
// treated as ~6500 K black-body light, re-colored to 6500·g K, brightness ∝ g⁴.
vec3 frequencyShift(vec3 c, float g) {
    if (abs(g - 1.0) < 1e-3) return c;
    vec3 tint = blackbody(6500.0 * g) / max(blackbody(6500.0), vec3(1e-4));
    return c * tint * min(g * g * g * g, 64.0);
}

vec3 sky(vec3 dir) { return textureLod(uSky, dir, 0.0).rgb * uSkyBrightness; }

// Light emitted by the particle volume along the (short, straight) chord a -> b
// of a ray step, in world coordinates. Optically thin: just integrate emissivity.
vec3 volumeLight(vec3 a, vec3 b) {
    if (!uVolumeOn) return vec3(0.0);
    vec3 lo = min(a, b), hi = max(a, b);
    if (any(greaterThan(lo, uVolumeMin + uVolumeSize)) || any(lessThan(hi, uVolumeMin))) return vec3(0.0);
    float len = length(b - a);
    int n = clamp(int(ceil(len / uVoxel)), 1, 96);
    vec3 sum = vec3(0.0);
    for (int k = 0; k < n; ++k) {
        vec3 p = mix(a, b, (float(k) + 0.5) / float(n));
        sum += textureLod(uVolume, (p - uVolumeMin) / uVolumeSize, 0.0).rgb;
    }
    return sum * (len / float(n));
}

vec3 accel(vec3 x, float h2) {
    float r2 = dot(x, x);
    return -3.0 * h2 * x / (r2 * r2 * sqrt(r2));
}

// Color of the disk at Boyer-Lindquist radius r and world azimuth phi, seen with redshift g.
vec3 diskShade(float r, float phi, float omega, float g) {
    // Turbulent texture carried around at the local orbital speed (it winds up over time).
    float a = phi + omega * uTime;
    vec3 q = vec3(cos(a), sin(a), log(r) * 1.6) * vec3(3.0, 3.0, 4.0);
    float turbulence = 0.45 + 1.1 * fbm(q);
    float edge = smoothstep(uDiskRadii.y, uDiskRadii.y * 0.8, r) * smoothstep(uDiskRadii.x, uDiskRadii.x * 1.04, r);

    float t = diskTemperatureIn(r, uIsco, uHorizon);
    float tObserved = t * (uColorShift ? g : 1.0);
    float intensity = uDiskBrightness * t * t * t * t * turbulence * edge * (uBeaming ? g * g * g * g : 1.0);
    return blackbody(tObserved * uPeakTemperature) * intensity;
}

float cameraFactor() { return gCamera; }

// Schwarzschild: light reaching the camera from the disk point p; vRay is the
// backward ray's direction there, so the physical photon moves along -vRay.
vec3 diskEmission(vec3 p, vec3 vRay) {
    float r = length(p);
    vec3 vPhoton = -vRay;
    vec3 L = cross(p, vPhoton);
    float rdot = dot(p, vPhoton) / r;
    float energy = sqrt(max(rdot * rdot + (1.0 - 2.0 / r) * dot(L, L) / (r * r), 1e-12));

    // Keplerian circular orbit (angular momentum along +y): Ω = sqrt(M/r³), u^t = 1/sqrt(1-3M/r).
    // g = ν_obs/ν_emit = 1 / (u^t (1 - Ω L_y / E)), then from infinity to the (static) camera.
    float omega = sqrt(1.0 / (r * r * r));
    float ut = 1.0 / sqrt(max(1.0 - 3.0 / r, 1e-4));
    float g = cameraFactor() / (ut * (1.0 - omega * L.y / energy));
    return diskShade(r, atan(p.z, p.x), omega, g);
}

// ---- Kerr-Schild (formula axes: spin along Z; world -> formula: X = z, Y = x, Z = y) ----
vec3 toKS(vec3 w) { return vec3(w.z, w.x, w.y); }
vec3 fromKS(vec3 k) { return vec3(k.y, k.z, k.x); }

float ksRadius(vec3 X, float a) {
    float b = dot(X, X) - a * a;
    return sqrt(0.5 * (b + sqrt(b * b + 4.0 * a * a * X.z * X.z)));
}

// Hamilton's equations with p_t = -1 (see src/core/Kerr.hpp for the derivation).
void ksDerivatives(vec3 X, vec3 p, float a, out vec3 dX, out vec3 dp) {
    float r = ksRadius(X, a);
    float r2 = r * r, a2 = a * a, Z = X.z;
    float q = r2 + a2;
    float sigma = r2 * r2 + a2 * Z * Z;
    float f = 2.0 * r2 * r / sigma;
    vec3 l = vec3((r * X.x + a * X.y) / q, (r * X.y - a * X.x) / q, Z / r);
    float lp = 1.0 + dot(l, p);
    dX = p - f * lp * l;

    vec3 dr = vec3(r2 * X.x, r2 * X.y, q * Z) * (r / sigma);
    vec3 df = f * (3.0 * dr / r - (4.0 * r2 * r * dr + vec3(0.0, 0.0, 2.0 * a2 * Z)) / sigma);
    float nX = r * X.x + a * X.y, nY = r * X.y - a * X.x;
    vec3 dq = 2.0 * r * dr;
    vec3 dlX = ((dr * X.x + vec3(r, a, 0.0)) * q - nX * dq) / (q * q);
    vec3 dlY = ((dr * X.y + vec3(-a, r, 0.0)) * q - nY * dq) / (q * q);
    vec3 dlZ = (vec3(0.0, 0.0, r) - Z * dr) / r2;
    dp = 0.5 * lp * lp * df + f * lp * (p.x * dlX + p.y * dlY + p.z * dlZ);
}

// Spatial momentum along dir that makes the photon null for p_t = -1.
vec3 ksNullMomentum(vec3 X, vec3 dir, float a) {
    float r = ksRadius(X, a);
    float r2 = r * r, q = r2 + a * a;
    float f = 2.0 * r2 * r / (r2 * r2 + a * a * X.z * X.z);
    vec3 l = vec3((r * X.x + a * X.y) / q, (r * X.y - a * X.x) / q, X.z / r);
    float ln = dot(l, dir);
    float A = 1.0 - f * ln * ln, B = -2.0 * f * ln, C = -(1.0 + f);
    return dir * ((-B + sqrt(B * B - 4.0 * A * C)) / (2.0 * A));
}

// Kerr disk point X (formula axes, Z = 0); lz is the physical photon's L_z (E = 1).
vec3 kerrDiskEmission(vec3 X, float lz, float a) {
    float r = ksRadius(X, a);
    // Prograde circular equatorial orbit: Ω = 1/(r^1.5 + a), u^t from Bardeen et al. (1972).
    float r15 = r * sqrt(r);
    float omega = 1.0 / (r15 + a);
    float ut = (r15 + a) / (sqrt(sqrt(r * r * r)) * sqrt(max(r15 - 3.0 * sqrt(r) + 2.0 * a, 1e-4)));
    float g = clamp(cameraFactor() / (ut * (1.0 - omega * lz)), 0.0, 20.0);
    vec3 w = fromKS(X);
    return diskShade(r, atan(w.z, w.x), omega, g);
}

// Tracing light backwards in time near a rotating hole: Kerr is symmetric under
// (t -> -t, a -> -a), so instead of integrating backwards (which runs into the
// past horizon, where ingoing Kerr-Schild coordinates break down) we integrate
// the reversed ray FORWARD in the mirrored hole with spin -a. It then crosses the
// future horizon cleanly. E, L_z (up to sign), the Boyer-Lindquist radius of the
// disk crossing and the escape direction are the same in both pictures; only
// the azimuth near the hole is twisted, which an axisymmetric disk does not mind.
vec3 traceKerr(vec3 dir) {
    float a = -uSpin;  // mirrored hole
    vec3 X = toKS(uCameraPos);
    vec3 p = ksNullMomentum(X, toKS(dir), a);
    float escapeRadius = max(length(uCameraPos), max(uDiskRadii.y, uVolumeOn ? 0.87 * uVolumeSize : 0.0)) + 20.0;
    vec3 light = vec3(0.0);
    for (int i = 0; i < uMaxSteps; ++i) {
        float r = ksRadius(X, a);
        if (r < uHorizon) return light;  // crossed the horizon: shadow
        float h = uStepScale * r;
        vec3 X0 = X, p0 = p;
        vec3 dx1, dp1, dx2, dp2, dx3, dp3, dx4, dp4;
        ksDerivatives(X0, p0, a, dx1, dp1);
        ksDerivatives(X0 + 0.5 * h * dx1, p0 + 0.5 * h * dp1, a, dx2, dp2);
        ksDerivatives(X0 + 0.5 * h * dx2, p0 + 0.5 * h * dp2, a, dx3, dp3);
        ksDerivatives(X0 + h * dx3, p0 + h * dp3, a, dx4, dp4);
        X = X0 + (h / 6.0) * (dx1 + 2.0 * dx2 + 2.0 * dx3 + dx4);
        p = p0 + (h / 6.0) * (dp1 + 2.0 * dp2 + 2.0 * dp3 + dp4);
        light += volumeLight(fromKS(X0), fromKS(X));

        if (uDisk && X0.z * X.z < 0.0) {
            float s = X0.z / (X0.z - X.z);
            vec3 Xd = mix(X0, X, s);
            vec3 pd = mix(p0, p, s);
            float rd = ksRadius(Xd, a);
            // The physical photon runs the other way: L_z flips sign.
            if (rd > uDiskRadii.x && rd < uDiskRadii.y) return light + kerrDiskEmission(Xd, -(Xd.x * pd.y - Xd.y * pd.x), uSpin);
        }
        vec3 step = X - X0;
        if (r > escapeRadius && dot(X, step) > 0.0) return light + sky(fromKS(normalize(step)));
    }
    return light;
}

void main() {
    vec4 farPoint = uInvViewProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - uCameraPos);
    if (!uLensing) {
        fragColor = vec4(sky(dir), 1.0);
        return;
    }
    if (abs(uSpin) > 1e-4 && !uRainFrame) {
        gCamera = 1.0 / sqrt(max(1.0 - 2.0 / length(uCameraPos), 1e-4));  // static camera
        fragColor = vec4(traceKerr(dir), 1.0);
        return;
    }

    // From the camera's local frame to the starting direction of the ray's
    // spatial path (pseudo-Cartesian Schwarzschild), plus the camera's frequency
    // factor. With d_r the radial (outward) part of the pixel direction:
    //   static observer: path ∝ r̂ sqrt(1-2M/r) d_r + d_⊥,  g = 1/sqrt(1-2M/r)
    //   rain observer:   path ∝ r̂ (d_r + β) + d_⊥,         g = 1/(1 + β d_r), β = sqrt(2M/r)
    vec3 x = uCameraPos;
    float rCam = length(x);
    vec3 n = x / rCam;
    float dr = dot(dir, n);
    vec3 dPerp = dir - n * dr;
    vec3 v;
    if (uRainFrame) {
        float beta = sqrt(2.0 / rCam);
        float denominator = 1.0 + beta * dr;  // = E / ν_camera
        // Inside the horizon (β > 1), looking towards the center: such photons
        // would carry negative energy E; they could only come from a white hole,
        // which real black holes do not have. That cone is the shadow ahead.
        if (denominator <= 0.02) {
            fragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        v = normalize(n * (dr + beta) + dPerp);
        gCamera = min(1.0 / denominator, 20.0);
    } else {
        float f = max(1.0 - 2.0 / rCam, 1e-4);
        v = normalize(n * (sqrt(f) * dr) + dPerp);
        gCamera = 1.0 / sqrt(f);
    }

    vec3 Lv = cross(x, v);
    float h2 = dot(Lv, Lv);
    float escapeRadius = max(rCam, max(uDiskRadii.y, uVolumeOn ? 0.87 * uVolumeSize : 0.0)) + 20.0;

    vec3 color = vec3(0.0);
    vec3 particles = vec3(0.0);
    for (int i = 0; i < uMaxSteps; ++i) {
        float r = length(x);
        // Captured: inside the horizon and still heading inwards (backwards in time).
        // A camera inside the horizon traces outwards first, so it is not cut off.
        if (r < 2.0005 && dot(x, v) < 0.0) break;
        if (r < 0.05) break;
        if (r > escapeRadius && dot(x, v) > 0.0) {
            color += frequencyShift(sky(normalize(v)), gCamera);
            break;
        }

        // Classic RK4 in the affine parameter; steps shrink near the hole.
        float h = uStepScale * r;
        vec3 x0 = x, v0 = v;
        vec3 k1x = v0, k1v = accel(x0, h2);
        vec3 k2x = v0 + 0.5 * h * k1v, k2v = accel(x0 + 0.5 * h * k1x, h2);
        vec3 k3x = v0 + 0.5 * h * k2v, k3v = accel(x0 + 0.5 * h * k2x, h2);
        vec3 k4x = v0 + h * k3v, k4v = accel(x0 + h * k3x, h2);
        x = x0 + (h / 6.0) * (k1x + 2.0 * k2x + 2.0 * k3x + k4x);
        v = v0 + (h / 6.0) * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);
        particles += volumeLight(x0, x);

        // Opaque thin disk in the equatorial plane y = 0.
        if (uDisk && x0.y * x.y < 0.0) {
            float s = x0.y / (x0.y - x.y);
            vec3 p = mix(x0, x, s);
            float rp = length(p);
            if (rp > uDiskRadii.x && rp < uDiskRadii.y) {
                color += diskEmission(p, normalize(mix(v0, v, s)));
                break;
            }
        }
    }
    fragColor = vec4(color + frequencyShift(particles, gCamera), 1.0);
}
