// Adds weight × source into the (additively blended) accumulation target.
out vec4 fragColor;

uniform sampler2D uSource;
uniform float uWeight;

void main() {
    fragColor = texelFetch(uSource, ivec2(gl_FragCoord.xy), 0) * uWeight;
}
