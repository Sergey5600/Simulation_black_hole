// Story layer ("peach" journey): events in the outside universe. They live in
// the sky (directions in the journey frame: radial out, side, up) and run on
// uCosmicTime, the outside clock as seen by the camera (0 = start, 1 = release),
// so hovering near the horizon makes them pile up. Drawn where a ray escapes,
// they are lensed, multiply imaged and shifted exactly like the stars.
// Placement, light curves and the time-lapse are artistic, not a catalog.

uniform bool uStory;
uniform float uCosmicTime;
uniform mat3 uStoryFrame;  // columns: radial out, side, up (world)

// xyz = direction (journey frame), w = peak time. #0 sits behind the hole on the
// way down (an Einstein ring), #3 is a neutron star merger (kilonova).
const int kEventCount = 6;
const vec4 kEvents[kEventCount] = vec4[](
    vec4(-1.0, 0.07, 0.04, 0.085), vec4(0.50, 0.60, 0.62, 0.30), vec4(0.20, -0.70, 0.68, 0.46),
    vec4(0.75, 0.30, -0.59, 0.57), vec4(-0.30, -0.40, -0.87, 0.70), vec4(0.90, -0.10, 0.42, 0.86));
const int kKilonova = 3;

// xyz = direction, w = angular radius (rad); they fade out one after another.
const int kGalaxyCount = 6;
const vec4 kGalaxies[kGalaxyCount] = vec4[](
    vec4(0.55, -0.45, 0.70, 0.15), vec4(-0.20, 0.85, 0.48, 0.12), vec4(0.70, 0.55, -0.45, 0.18),
    vec4(-0.60, -0.70, 0.38, 0.13), vec4(0.30, -0.20, -0.93, 0.16), vec4(0.85, 0.05, 0.52, 0.11));

// Angular offset of `dir` from `center` in the tangent plane at `center`.
vec2 tangentOffset(vec3 dir, vec3 center) {
    vec3 a = normalize(cross(center, abs(center.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(center, a);
    return vec2(dot(dir, a), dot(dir, b)) / max(dot(dir, center), 1e-3);
}

// A black body of temperature T and luminance L seen with shift g.
vec3 shiftedSource(float luminance, float kelvin, float g) {
    vec3 emitted = planckRgb(kelvin);
    return planckRgb(kelvin * g) * (luminance / max(dot(emitted, kLuma), 1e-30));
}

vec3 supernova(float age, float g, float glow) {
    if (age < -0.02) return vec3(0.0);
    // Fast rise, then the radioactive decline; cools from blue-white to yellow.
    float light = age < 0.0 ? exp(-age * age / 3.6e-5) : exp(-age / 0.05) + 0.12 * exp(-age / 0.25);
    float kelvin = mix(14000.0, 5000.0, clamp(age / 0.15, 0.0, 1.0));
    return shiftedSource(200.0 * light * glow, kelvin, g);
}

vec3 kilonova(float age, float g, float glow) {
    if (age < -0.01) return vec3(0.0);
    // A gamma-ray burst flash, then a fast blue glow turning red (r-process ejecta).
    vec3 flash = shiftedSource(2000.0 * exp(-age * age / 6e-6) * glow, 40000.0, g);
    float light = age < 0.0 ? exp(-age * age / 4e-6) : exp(-age / 0.02);
    float kelvin = mix(9000.0, 2500.0, clamp(age / 0.06, 0.0, 1.0));
    return flash + shiftedSource(100.0 * light * glow, kelvin, g);
}

vec3 galaxy(vec2 q, float radius, int index, float g) {
    float fade = 1.0 - smoothstep(0.25 + 0.11 * float(index), 0.40 + 0.11 * float(index), uCosmicTime);
    if (fade <= 0.0) return vec3(0.0);
    // Inclined spiral: exponential disk with two log-spiral arms and a bulge.
    float angle = 1.3 * float(index);
    vec2 p = mat2(cos(angle), -sin(angle), sin(angle), cos(angle)) * q / radius;
    p.y /= 0.35 + 0.1 * float(index % 3);
    float rho = length(p);
    if (rho > 3.0) return vec3(0.0);
    float arms = 0.55 + 0.45 * cos(2.0 * atan(p.y, p.x) - 5.0 * log(rho + 0.05));
    float disk = exp(-rho / 0.35) * arms;
    float bulge = exp(-rho * rho / 0.01);
    // Receding into the cosmic future: dimmer and redder (cosmological redshift).
    float redshift = 0.45 + 0.55 * fade;
    return (shiftedSource(0.05 * disk, 7000.0 * redshift, g) + shiftedSource(0.25 * bulge, 4500.0 * redshift, g)) *
           fade * fade;
}

// Light of all events seen in world direction `dir` with shift g.
vec3 skyEvents(vec3 dir, float g) {
    if (!uStory) return vec3(0.0);
    vec3 d = transpose(uStoryFrame) * dir;  // journey frame
    vec3 light = vec3(0.0);
    const float kSpot = 0.021;  // angular size of a "point" source (1.2°, so it survives the compression)
    for (int i = 0; i < kEventCount; ++i) {
        vec3 center = normalize(kEvents[i].xyz);
        float cosAngle = dot(d, center);
        if (cosAngle < 0.98) continue;
        float angle2 = 2.0 * (1.0 - cosAngle);
        float glow = exp(-angle2 / (2.0 * kSpot * kSpot));
        float age = uCosmicTime - kEvents[i].w;
        light += i == kKilonova ? kilonova(age, g, glow) : supernova(age, g, glow);
    }
    for (int i = 0; i < kGalaxyCount; ++i) {
        vec3 center = normalize(kGalaxies[i].xyz);
        if (dot(d, center) < cos(3.2 * kGalaxies[i].w)) continue;
        light += galaxy(tangentOffset(d, center), kGalaxies[i].w, i, g);
    }
    return light;
}

// The rest of the sky ages too: stars burn out, the expansion redshifts what is left.
float cosmicDimming() { return uStory ? mix(1.0, 0.3, smoothstep(0.3, 1.0, uCosmicTime)) : 1.0; }
float cosmicRedshift() { return uStory ? mix(1.0, 0.8, smoothstep(0.3, 1.0, uCosmicTime)) : 1.0; }

// Story finale. ARTISTIC, not physics: near r = 0 the tides already squeeze the
// view into a thin sheet across the fall where the light is blueshifted without
// bound (that part is real and comes out of the ray tracer). General relativity
// ends at the singularity; this layer imagines it as that sheet widening into
// streaks of light, whiting out the frame, and going dark when proper time ends.
uniform float uSingularity;  // 0 = off .. 1 = the end of proper time

vec3 singularityLight(vec3 dir, vec3 radial) {
    if (uSingularity <= 0.0) return vec3(0.0);
    float s = uSingularity;
    float across = dot(dir, radial);  // 0 in the squeeze plane
    vec3 e1 = normalize(cross(radial, abs(radial.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 e2 = cross(radial, e1);
    float azimuth = atan(dot(dir, e2), dot(dir, e1));
    float width = mix(0.003, 0.4, s * s * s * s);
    float sheet = exp(-across * across / (2.0 * width * width));
    float streaks = 0.5 + 0.5 * fbm(vec3(azimuth * 7.0, s * 4.0, 1.7));
    float halo = 0.1 * exp(-abs(across) / (0.03 + 0.25 * s));
    float whiteout = 600.0 * smoothstep(0.85, 0.95, s);
    // Fade in log space: the white-out sits far above display white.
    float fade = smoothstep(0.95, 1.0, s);
    float end = exp2(-16.0 * fade) * (1.0 - fade);
    vec3 hot = planckRgb(40000.0) / dot(planckRgb(40000.0), kLuma);
    return hot * ((sheet * streaks + halo) * 60.0 * s * s * s + whiteout) * end;
}
