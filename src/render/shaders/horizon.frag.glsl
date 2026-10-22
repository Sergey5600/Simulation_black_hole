// Event horizon (r = 2M) as an exact ray-traced sphere that writes depth, so
// particles behind it are hidden. No lensing yet: that is the ray-tracing stage.
in vec2 vUv;
layout(location = 0) out vec4 fragColor;
// Black in the compositing layers too (lensing.frag.glsl), when they are drawn.
layout(location = 1) out vec4 layerSky;
layout(location = 2) out vec4 layerDisk;
layout(location = 3) out vec4 layerParticles;
layout(location = 4) out vec4 layerEvents;
layout(location = 5) out vec4 layerPeach;
layout(location = 6) out vec4 layerSingularity;

uniform mat4 uInvViewProj;
uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform float uRadius;

void main() {
    vec4 farPoint = uInvViewProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - uCameraPos);

    float b = dot(uCameraPos, dir);
    float c = dot(uCameraPos, uCameraPos) - uRadius * uRadius;
    float disc = b * b - c;
    if (disc < 0.0) discard;
    float t = -b - sqrt(disc);
    if (t < 0.0) discard;

    vec4 clip = uViewProj * vec4(uCameraPos + dir * t, 1.0);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
    layerSky = vec4(0.0);
    layerDisk = vec4(0.0);
    layerParticles = vec4(0.0);
    layerEvents = vec4(0.0);
    layerPeach = vec4(0.0);
    layerSingularity = vec4(0.0);
}
