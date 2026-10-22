// Stars baked into the sky cube map. Alpha carries luminance x temperature, so
// that after additive blending a/luminance is the light-weighted temperature
// of the texel; the ray tracer shifts it as a black body (lensing.frag.glsl).
in vec4 vColorTemperature;
out vec4 fragColor;

void main() {
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(d, d);
    if (r2 > 1.0) discard;
    vec3 c = vColorTemperature.rgb * exp(-4.0 * r2);
    fragColor = vec4(c, dot(c, kLuma) * vColorTemperature.w);
}
