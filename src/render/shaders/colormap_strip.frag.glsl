// Renders a horizontal colormap strip (used for the UI legend), sRGB-encoded.
in vec2 vUv;
out vec4 fragColor;

uniform int uColormap;

void main() {
    fragColor = vec4(pow(colormap(uColormap, vUv.x), vec3(1.0 / 2.2)), 1.0);
}
