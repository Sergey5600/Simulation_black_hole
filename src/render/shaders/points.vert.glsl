// One sprite per particle. Colors are computed per camera (particle_shading.glsl)
// because the Doppler factor depends on the direction to the viewer.
layout(location = 0) in vec4 aPosScalar;  // see PointVertex
layout(location = 1) in vec4 aDirValue;

uniform mat4 uView;
uniform mat4 uProj;
uniform float uPixelsPerUnit;  // projection scale: pixels per world unit at distance 1
uniform float uPointSize;      // world-space sprite diameter

out vec3 vColor;

void main() {
    vec3 radiance;
    if (!shadeParticle(aPosScalar, aDirValue, radiance)) {  // dead slot: push outside the clip volume
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 0.0;
        vColor = vec3(0.0);
        return;
    }
    vec4 viewPos = uView * vec4(aPosScalar.xyz, 1.0);
    gl_Position = uProj * viewPos;

    // Perspective-correct sprite size. Sub-pixel sprites are clamped and dimmed so
    // that distant regions keep the right total brightness instead of aliasing.
    float size = uPointSize * uPixelsPerUnit / max(-viewPos.z, 1e-3);
    const float kMinSize = 1.5;
    if (size < kMinSize) {
        radiance *= (size * size) / (kMinSize * kMinSize);
        size = kMinSize;
    }
    gl_PointSize = min(size, 64.0);
    vColor = radiance;
}
