// Box filter: every output pixel averages a factor x factor block of the source.
out vec4 fragColor;

uniform sampler2D uSource;
uniform int uFactor;

void main() {
    ivec2 base = ivec2(gl_FragCoord.xy) * uFactor;
    vec4 sum = vec4(0.0);
    for (int y = 0; y < uFactor; ++y)
        for (int x = 0; x < uFactor; ++x) sum += texelFetch(uSource, base + ivec2(x, y), 0);
    fragColor = sum / float(uFactor * uFactor);
}
