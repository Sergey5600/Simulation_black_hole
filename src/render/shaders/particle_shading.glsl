// Color of one particle as seen from the camera, shared by the sprite renderer
// (points.vert) and the volume splatter used for lensing (splat.comp).
// Needs common.glsl. Returns false for dead slots.

uniform vec3 uCameraPos;
uniform bool uPhotons;
uniform bool uScalarIsBrightness;  // gas: per-particle brightness grain in posScalar.w

uniform int uStyle;
uniform int uQuantity;
uniform int uColormap;
uniform vec2 uRange;
uniform float uBrightness;
uniform float uPeakTemperature;
uniform bool uBeaming;
uniform bool uColorShift;
uniform float uLifetime;      // photons: age at which a slot is recycled (0 = unknown)
uniform bool uFadeWithAge;

float quantityValue(int q, float r, float g, vec4 posScalar, vec4 dirValue) {
    if (q == Q_TEMPERATURE) return diskTemperature(r);
    if (q == Q_RADIUS) return r;
    if (q == Q_SPEED) return length(dirValue.xyz);
    if (q == Q_REDSHIFT) return g;
    if (q == Q_TIME_DILATION) return dirValue.w;
    if (q == Q_AGE) return uLifetime > 0.0 ? posScalar.w / uLifetime : posScalar.w;  // phase 0..1
    if (q == Q_TAG) return posScalar.w;
    return dirValue.w;  // Q_IMPACT
}

// posScalar: xyz = position (M), w = scalar; dirValue: xyz = β or photon direction,
// w = dτ/dt or b (< 0: dead). See PointVertex in src/sim/Particle.hpp.
bool shadeParticle(vec4 posScalar, vec4 dirValue, out vec3 radiance) {
    radiance = vec3(0.0);
    if (dirValue.w < 0.0) return false;

    vec3 pos = posScalar.xyz;
    float r = length(pos);
    vec3 toCamera = normalize(uCameraPos - pos);
    // Redshift towards the camera: g = (dτ/dt) / (1 - β·n), with a straight-line
    // photon direction (exact for the ray-traced thin disk, approximate here).
    float g = uPhotons ? 1.0 : dirValue.w / max(1.0 - dot(dirValue.xyz, toCamera), 1e-3);

    vec3 color;
    float intensity = uBrightness;
    if (uStyle == STYLE_BLACKBODY && !uPhotons) {
        float t = diskTemperature(r);
        float tObserved = t * (uColorShift ? g : 1.0);
        color = blackbody(tObserved * uPeakTemperature);
        intensity *= t * t * t * t;  // Stefan-Boltzmann: emitted flux ∝ T⁴
    } else {
        float v = quantityValue(uQuantity, r, g, posScalar, dirValue);
        color = colormap(uColormap, (v - uRange.x) / (uRange.y - uRange.x));
    }
    if (uScalarIsBrightness) intensity *= posScalar.w;      // clumpy gas
    if (uBeaming && !uPhotons) intensity *= g * g * g * g;  // relativistic beaming, I ∝ g⁴

    if (uPhotons && uFadeWithAge && uLifetime > 0.0) {
        float age = posScalar.w / uLifetime;
        intensity *= smoothstep(0.0, 0.02, age) * (1.0 - smoothstep(0.7, 1.0, age));
    }
    radiance = color * intensity;
    return true;
}
