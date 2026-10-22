// Background + gravitational lensing. Without lensing: the sky along straight
// camera rays. With lensing: every pixel traces a photon geodesic backwards
// from the camera using the exact Schwarzschild null equation in pseudo-Cartesian
// form, x'' = -3M h² x / r⁵ (h = |x × x'|), until the ray
//   * falls through the horizon      -> black (the shadow),
//   * crosses the thin disk          -> Novikov-Thorne emission with the exact redshift,
//   * escapes                        -> the sky in its asymptotic direction.
in vec2 vUv;
out vec4 fragColor;

uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform samplerCube uSky;
uniform float uSkyBrightness;
uniform bool uLensing;

uniform bool uDisk;
uniform vec2 uDiskRadii;       // inner, outer (M)
uniform float uDiskBrightness;
uniform float uPeakTemperature;
uniform bool uBeaming;
uniform bool uColorShift;
uniform float uTime;           // drives the differential rotation of the disk texture

uniform int uMaxSteps;
uniform float uStepScale;      // affine step as a fraction of r

vec3 sky(vec3 dir) { return textureLod(uSky, dir, 0.0).rgb * uSkyBrightness; }

vec3 accel(vec3 x, float h2) {
    float r2 = dot(x, x);
    return -3.0 * h2 * x / (r2 * r2 * sqrt(r2));
}

// Light reaching the camera from the disk point p; vRay is the backward ray's
// direction there, so the physical photon moves along -vRay.
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
    float g = 1.0 / (ut * (1.0 - omega * L.y / energy));
    g /= sqrt(max(1.0 - 2.0 / length(uCameraPos), 1e-4));

    // Turbulent texture carried around at the local orbital speed (it winds up over time).
    float phi = atan(p.z, p.x) + omega * uTime;
    vec3 q = vec3(cos(phi), sin(phi), log(r) * 1.6) * vec3(3.0, 3.0, 4.0);
    float turbulence = 0.45 + 1.1 * fbm(q);
    float edge = smoothstep(uDiskRadii.y, uDiskRadii.y * 0.8, r) * smoothstep(uDiskRadii.x, uDiskRadii.x * 1.04, r);

    float t = diskTemperature(r);
    float tObserved = t * (uColorShift ? g : 1.0);
    float intensity = uDiskBrightness * t * t * t * t * turbulence * edge * (uBeaming ? g * g * g * g : 1.0);
    return blackbody(tObserved * uPeakTemperature) * intensity;
}

void main() {
    vec4 farPoint = uInvViewProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - uCameraPos);
    if (!uLensing) {
        fragColor = vec4(sky(dir), 1.0);
        return;
    }

    vec3 x = uCameraPos;
    vec3 v = dir;
    vec3 Lv = cross(x, v);
    float h2 = dot(Lv, Lv);
    float escapeRadius = max(length(uCameraPos), uDiskRadii.y) + 20.0;

    vec3 color = vec3(0.0);
    for (int i = 0; i < uMaxSteps; ++i) {
        float r = length(x);
        if (r < 2.0005) break;  // captured: shadow
        if (r > escapeRadius && dot(x, v) > 0.0) {
            color = sky(normalize(v));
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

        // Opaque thin disk in the equatorial plane y = 0.
        if (uDisk && x0.y * x.y < 0.0) {
            float s = x0.y / (x0.y - x.y);
            vec3 p = mix(x0, x, s);
            float rp = length(p);
            if (rp > uDiskRadii.x && rp < uDiskRadii.y) {
                color = diskEmission(p, normalize(mix(v0, v, s)));
                break;
            }
        }
    }
    fragColor = vec4(color, 1.0);
}
