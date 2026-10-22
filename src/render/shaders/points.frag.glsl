in vec3 vColor;
out vec4 fragColor;

void main() {
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(d, d);
    if (r2 > 1.0) discard;
    // Gaussian splat; additive blending turns many of them into a glowing volume.
    fragColor = vec4(vColor * exp(-4.0 * r2), 1.0);
}
