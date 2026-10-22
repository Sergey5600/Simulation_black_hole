// Story layer ("peach" journey): a peach floating next to the camera. At a
// meter from the lens spacetime is flat to ~1e-10 even at the horizon, so the
// peach is ray-marched along straight rays in the camera's local frame (meters,
// world-aligned axes) in front of the traced background. It co-moves with the
// camera, so its light needs no shift. Lighting: spherical harmonics of the
// lensed, shifted environment seen from the camera (Renderer probe), plus a
// small lamp on the camera. Tides stretch it along uStretchAxis once torn.

uniform bool uPeachOn;
uniform vec3 uPeachCenter;   // m, from the camera
uniform mat3 uPeachBasis;    // body axes (columns): x, y = stem, z = suture side
uniform vec3 uStretchAxis;   // radial direction (unit)
uniform vec2 uPeachScale;    // stretch along the axis, squeeze across it
uniform vec3 uPeachSH[9];    // irradiance / π: 1, y, z, x, xy, yz, 3z²-1, xz, x²-y²
uniform vec3 uPeachLamp;     // camera lamp: radiance-scaled color

const float kPeachRadius = 0.038;  // m

// Body-frame SDF (meters). Material: 0 skin, 1 stem, 2 leaf.
float peachBody(vec3 p, out int material) {
    // Slightly flattened, with a suture groove along one meridian and a stem dimple.
    vec3 q = p / vec3(1.0, 0.92, 0.97);
    float d = length(q) - kPeachRadius;
    float groove = exp(-p.x * p.x / (0.0016 * kPeachRadius * kPeachRadius * 9.0)) * smoothstep(-0.3, 0.8, p.z / kPeachRadius);
    float dimple = exp(-dot(p.xz, p.xz) / (0.06 * kPeachRadius * kPeachRadius)) * smoothstep(0.4, 1.0, p.y / kPeachRadius);
    d = (d + 0.07 * kPeachRadius * groove + 0.22 * kPeachRadius * dimple) * 0.85;
    material = 0;

    // Stem: a short tilted capsule out of the dimple.
    vec3 s = p - vec3(0.0, 0.80 * kPeachRadius, 0.0);
    vec3 axis = normalize(vec3(0.25, 1.0, 0.1));
    float h = clamp(dot(s, axis), 0.0, 0.32 * kPeachRadius);
    float stem = length(s - axis * h) - 0.07 * kPeachRadius;
    if (stem < d) { d = stem; material = 1; }

    // Leaf: a thin curved ellipsoid off the stem.
    vec3 l = p - vec3(0.32 * kPeachRadius, 1.02 * kPeachRadius, 0.12 * kPeachRadius);
    float c = cos(0.55), sn = sin(0.55);
    l = vec3(c * l.x - sn * l.y, sn * l.x + c * l.y, l.z);
    l.y += 2.2 * l.x * l.x / kPeachRadius;  // curl
    float leaf = (length(l / vec3(0.85, 0.06, 0.32)) - kPeachRadius) * 0.06;
    if (leaf < d) { d = leaf; material = 2; }
    return d;
}

// Camera-local point -> body frame, undoing the tidal deformation.
vec3 peachToBody(vec3 x) {
    vec3 rel = x - uPeachCenter;
    float along = dot(rel, uStretchAxis);
    vec3 undeformed = uStretchAxis * (along / uPeachScale.x) + (rel - uStretchAxis * along) / uPeachScale.y;
    return transpose(uPeachBasis) * undeformed;
}

float peachSdf(vec3 x, out int material) {
    vec3 p = peachToBody(x);
    float d = peachBody(p, material);
    // Torn apart: the flesh pinches into beads along the stretch (tidal breakup).
    float torn = smoothstep(1.03, 2.0, uPeachScale.x);
    if (torn > 0.0) {
        float along = dot(transpose(uPeachBasis) * uStretchAxis, p) / kPeachRadius;
        d += torn * 0.35 * kPeachRadius * (0.5 + 0.5 * sin(along * 9.0 + 2.0 * fbm(p * 60.0)));
    }
    return d * min(uPeachScale.x, uPeachScale.y);  // a conservative distance under the deformation
}

vec3 peachIrradiance(vec3 n) {
    vec3 e = uPeachSH[0] + uPeachSH[1] * n.y + uPeachSH[2] * n.z + uPeachSH[3] * n.x + uPeachSH[4] * n.x * n.y +
             uPeachSH[5] * n.y * n.z + uPeachSH[6] * (3.0 * n.z * n.z - 1.0) + uPeachSH[7] * n.x * n.z +
             uPeachSH[8] * (n.x * n.x - n.y * n.y);
    return max(e, vec3(0.0));
}

// Light from the peach along camera-local direction `dir`; false if missed.
bool tracePeach(vec3 dir, out vec3 light) {
    light = vec3(0.0);
    if (!uPeachOn) return false;
    float bound = kPeachRadius * 1.45 * max(uPeachScale.x, 1.0);
    float b = dot(uPeachCenter, dir);
    float disc = b * b - dot(uPeachCenter, uPeachCenter) + bound * bound;
    if (disc <= 0.0) return false;
    float t = max(b - sqrt(disc), 0.0), tEnd = b + sqrt(disc);
    int material = 0;
    bool hit = false;
    for (int i = 0; i < 220 && t < tEnd; ++i) {
        float d = peachSdf(dir * t, material);
        if (d < 2e-5) {
            hit = true;
            break;
        }
        t += d;
    }
    if (!hit) return false;

    vec3 x = dir * t;
    const float e = 1e-4;
    int unused;
    vec3 n = normalize(vec3(peachSdf(x + vec3(e, 0, 0), unused) - peachSdf(x - vec3(e, 0, 0), unused),
                            peachSdf(x + vec3(0, e, 0), unused) - peachSdf(x - vec3(0, e, 0), unused),
                            peachSdf(x + vec3(0, 0, e), unused) - peachSdf(x - vec3(0, 0, e), unused)));
    vec3 p = peachToBody(x) / kPeachRadius;

    vec3 albedo;
    if (material == 1) {
        albedo = vec3(0.16, 0.09, 0.04);
    } else if (material == 2) {
        albedo = vec3(0.07, 0.22, 0.04) * (0.8 + 0.4 * fbm(p * 14.0));
    } else {
        // Golden skin with a red blush on one side, freckles, and the flesh where it tears.
        float blush = smoothstep(-0.4, 0.7, p.z + 0.35 * p.x + 0.5 * (fbm(p * 3.0) - 0.5));
        albedo = mix(vec3(0.80, 0.42, 0.08), vec3(0.50, 0.05, 0.035), blush);
        albedo *= 0.85 + 0.3 * fbm(p * 22.0);
        float flesh = smoothstep(1.2, 2.5, uPeachScale.x) * smoothstep(0.2, 0.7, fbm(p * 8.0));
        albedo = mix(albedo, vec3(0.95, 0.62, 0.20), flesh);
    }
    vec3 v = -dir;
    vec3 irradiance = peachIrradiance(n);
    vec3 lamp = uPeachLamp * max(dot(n, v), 0.0) / max(t * t, 0.04);
    // Peach fuzz scatters light at grazing angles (sheen).
    float sheen = material == 0 ? pow(1.0 - max(dot(n, v), 0.0), 3.0) : 0.0;
    vec3 ambient = peachIrradiance(v) + peachIrradiance(-v) + peachIrradiance(n);
    light = albedo * (irradiance + lamp) + sheen * 0.25 * ambient * vec3(1.0, 0.85, 0.7);
    return true;
}
