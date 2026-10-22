// Adds weight × source into the (additively blended) accumulation target.
out vec4 fragColor;

uniform sampler2D uSource;
uniform float uWeight;

void main() {
    fragColor = vec4(texelFetch(uSource, ivec2(gl_FragCoord.xy), 0).rgb * uWeight, 1.0);
}
