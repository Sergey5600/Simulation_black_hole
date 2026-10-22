// HDR scene + bloom -> exposure -> ACES filmic tone map -> sRGB.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform float uVignette;

// Stephen Hill's fit of the ACES RRT + ODT.
vec3 acesFitted(vec3 color) {
    const mat3 inputMat = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 outputMat = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    color = inputMat * color;
    vec3 a = color * (color + 0.0245786) - 0.000090537;
    vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return clamp(outputMat * (a / b), 0.0, 1.0);
}

vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main() {
    vec3 hdr = texture(uScene, vUv).rgb;
    vec3 bloom = texture(uBloom, vUv).rgb;
    vec3 color = mix(hdr, bloom, uBloomStrength) * uExposure;
    color = linearToSrgb(acesFitted(color));

    vec2 q = vUv - 0.5;
    color *= 1.0 - uVignette * dot(q, q) * 2.0;

    // Blue-noise-ish dither against banding in the dark gradients.
    float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    color += (n - 0.5) / 255.0;
    fragColor = vec4(color, 1.0);
}
