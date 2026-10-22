// 13-tap downsample (Jimenez, "Next Generation Post Processing in Call of Duty: AW").
// The first pass uses a Karis average to stop single bright particles from flickering.
in vec2 vUv;
out vec3 fragColor;

uniform sampler2D uSource;
uniform vec2 uTexel;  // 1 / source size
uniform bool uKaris;

float karisWeight(vec3 c) { return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }

void main() {
    vec2 x = uTexel;
    vec3 a = texture(uSource, vUv + vec2(-2, 2) * x).rgb;
    vec3 b = texture(uSource, vUv + vec2(0, 2) * x).rgb;
    vec3 c = texture(uSource, vUv + vec2(2, 2) * x).rgb;
    vec3 d = texture(uSource, vUv + vec2(-2, 0) * x).rgb;
    vec3 e = texture(uSource, vUv).rgb;
    vec3 f = texture(uSource, vUv + vec2(2, 0) * x).rgb;
    vec3 g = texture(uSource, vUv + vec2(-2, -2) * x).rgb;
    vec3 h = texture(uSource, vUv + vec2(0, -2) * x).rgb;
    vec3 i = texture(uSource, vUv + vec2(2, -2) * x).rgb;
    vec3 j = texture(uSource, vUv + vec2(-1, 1) * x).rgb;
    vec3 k = texture(uSource, vUv + vec2(1, 1) * x).rgb;
    vec3 l = texture(uSource, vUv + vec2(-1, -1) * x).rgb;
    vec3 m = texture(uSource, vUv + vec2(1, -1) * x).rgb;

    if (uKaris) {
        vec3 g0 = (a + b + d + e) * 0.25, g1 = (b + c + e + f) * 0.25;
        vec3 g2 = (d + e + g + h) * 0.25, g3 = (e + f + h + i) * 0.25;
        vec3 g4 = (j + k + l + m) * 0.25;
        float w0 = karisWeight(g0), w1 = karisWeight(g1), w2 = karisWeight(g2), w3 = karisWeight(g3);
        float w4 = karisWeight(g4);
        fragColor = (g0 * w0 * 0.125 + g1 * w1 * 0.125 + g2 * w2 * 0.125 + g3 * w3 * 0.125 + g4 * w4 * 0.5) /
                    (w0 * 0.125 + w1 * 0.125 + w2 * 0.125 + w3 * 0.125 + w4 * 0.5);
    } else {
        fragColor = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }
    fragColor = max(fragColor, vec3(0.0));
}
