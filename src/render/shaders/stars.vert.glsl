// Background stars at infinity: only the camera rotation applies.
layout(location = 0) in vec4 aDirMagnitude;  // xyz = direction, w = brightness
layout(location = 1) in vec4 aColorTemperature;  // rgb = color, w = temperature / 10⁴ K

uniform mat4 uViewRotation;
uniform mat4 uProj;
uniform float uScale;  // DPI scale

out vec4 vColorTemperature;

void main() {
    vec4 clip = uProj * uViewRotation * vec4(aDirMagnitude.xyz, 1.0);
    gl_Position = clip.xyww;  // depth = 1: always behind everything
    gl_PointSize = mix(1.5, 3.5, aDirMagnitude.w) * uScale;
    vColorTemperature = vec4(aColorTemperature.rgb * aDirMagnitude.w, aColorTemperature.w);
}
