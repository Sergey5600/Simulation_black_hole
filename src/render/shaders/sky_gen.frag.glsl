// Procedural sky for one cube-map face: a faint Milky Way band with dust lanes.
// Stars are drawn on top as points afterwards (Renderer::buildSky).
in vec2 vUv;
out vec4 fragColor;

uniform mat4 uInvViewProj;

void main() {
    vec4 p = uInvViewProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w);

    const vec3 galacticNormal = normalize(vec3(0.35, 0.86, 0.37));
    const vec3 galacticCenter = normalize(vec3(-0.80, 0.25, 0.55));
    float lat = dot(dir, galacticNormal);
    float band = exp(-lat * lat / 0.012);
    float bulge = pow(max(dot(dir, galacticCenter), 0.0), 6.0);

    float clouds = fbm(dir * 6.0);
    float dust = smoothstep(0.45, 0.75, fbm(dir * 11.0 + 7.0)) * exp(-lat * lat / 0.004);
    float glow = band * (0.2 + 1.0 * clouds * clouds) * (1.0 - 0.85 * dust) + bulge * band * 0.6 * clouds;

    vec3 color = mix(vec3(0.55, 0.62, 0.85), vec3(1.0, 0.82, 0.62), clamp(bulge * 2.0 + 0.2 * clouds, 0.0, 1.0));
    fragColor = vec4(color * glow * 0.025 + vec3(0.0008, 0.001, 0.002), 1.0);
}
