// One sprite per particle. Colors are computed here (per camera) because the
// Doppler factor depends on the direction to the viewer.
layout(location = 0) in vec4 aPosScalar;  // xyz = position (M), w = scalar (see PointVertex)
layout(location = 1) in vec4 aDirValue;   // xyz = β or photon direction, w = dτ/dt or b (<0: dead)

uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uCameraPos;
uniform float uPixelsPerUnit;  // projection scale: pixels per world unit at distance 1
uniform float uPointSize;      // world-space sprite diameter
uniform bool uPhotons;

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

out vec3 vColor;

float quantityValue(int q, float r, float g) {
    if (q == Q_TEMPERATURE) return diskTemperature(r);
    if (q == Q_RADIUS) return r;
    if (q == Q_SPEED) return length(aDirValue.xyz);
    if (q == Q_REDSHIFT) return g;
    if (q == Q_TIME_DILATION) return aDirValue.w;
    if (q == Q_AGE) return uLifetime > 0.0 ? aPosScalar.w / uLifetime : aPosScalar.w;  // phase 0..1
    return aDirValue.w;  // Q_IMPACT
}

void main() {
    if (aDirValue.w < 0.0) {  // dead slot: push outside the clip volume
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 0.0;
        vColor = vec3(0.0);
        return;
    }

    vec3 pos = aPosScalar.xyz;
    vec4 viewPos = uView * vec4(pos, 1.0);
    gl_Position = uProj * viewPos;

    float r = length(pos);
    vec3 toCamera = normalize(uCameraPos - pos);
    // Redshift towards the camera: g = (dτ/dt) / (1 - β·n). Straight-line photon
    // path for now; the lensing stage will use the true emission direction.
    float g = uPhotons ? 1.0 : aDirValue.w / max(1.0 - dot(aDirValue.xyz, toCamera), 1e-3);

    vec3 color;
    float intensity = uBrightness;
    if (uStyle == STYLE_BLACKBODY && !uPhotons) {
        float t = diskTemperature(r);
        float tObserved = t * (uColorShift ? g : 1.0);
        color = blackbody(tObserved * uPeakTemperature);
        intensity *= t * t * t * t;           // Stefan-Boltzmann: emitted flux ∝ T⁴
        intensity *= aPosScalar.w;            // clumpy gas
    } else {
        float v = quantityValue(uQuantity, r, g);
        color = colormap(uColormap, (v - uRange.x) / (uRange.y - uRange.x));
        if (!uPhotons) intensity *= aPosScalar.w;
    }
    if (uBeaming && !uPhotons) intensity *= g * g * g * g;  // relativistic beaming, I ∝ g⁴

    if (uPhotons && uFadeWithAge && uLifetime > 0.0) {
        float age = aPosScalar.w / uLifetime;
        intensity *= smoothstep(0.0, 0.02, age) * (1.0 - smoothstep(0.7, 1.0, age));
    }

    // Perspective-correct sprite size. Sub-pixel sprites are clamped and dimmed so
    // that distant regions keep the right total brightness instead of aliasing.
    float size = uPointSize * uPixelsPerUnit / max(-viewPos.z, 1e-3);
    const float kMinSize = 1.5;
    if (size < kMinSize) {
        intensity *= (size * size) / (kMinSize * kMinSize);
        size = kMinSize;
    }
    gl_PointSize = min(size, 64.0);
    vColor = color * intensity;
}
