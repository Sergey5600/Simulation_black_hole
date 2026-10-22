// 3x3 tent upsample, additively blended into the next larger mip.
in vec2 vUv;
out vec3 fragColor;

uniform sampler2D uSource;
uniform vec2 uRadius;  // filter radius in UV units

void main() {
    vec2 r = uRadius;
    vec3 c = texture(uSource, vUv).rgb * 4.0;
    c += (texture(uSource, vUv + vec2(-r.x, 0)).rgb + texture(uSource, vUv + vec2(r.x, 0)).rgb +
          texture(uSource, vUv + vec2(0, -r.y)).rgb + texture(uSource, vUv + vec2(0, r.y)).rgb) * 2.0;
    c += texture(uSource, vUv + vec2(-r.x, -r.y)).rgb + texture(uSource, vUv + vec2(r.x, -r.y)).rgb +
         texture(uSource, vUv + vec2(-r.x, r.y)).rgb + texture(uSource, vUv + vec2(r.x, r.y)).rgb;
    fragColor = c / 16.0;
}
