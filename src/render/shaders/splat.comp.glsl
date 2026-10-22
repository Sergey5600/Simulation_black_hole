// Deposits every particle's light into a 3D emission grid (trilinear weights,
// fixed-point atomics), so the lensing ray tracer can collect particle light
// along bent rays. One invocation per particle.
layout(local_size_x = 256) in;

struct Point {
    vec4 posScalar;
    vec4 dirValue;
};
layout(std430, binding = 0) readonly buffer Points { Point points[]; };

layout(r32ui, binding = 0) uniform coherent uimage3D uRed;
layout(r32ui, binding = 1) uniform coherent uimage3D uGreen;
layout(r32ui, binding = 2) uniform coherent uimage3D uBlue;

uniform uint uCount;
uniform vec3 uVolumeMin;
uniform float uVolumeSize;  // edge length (M)
uniform int uResolution;
uniform float uPointSize;   // matches the sprite look: luminosity = radiance × sprite area
uniform float uFixedScale;

void deposit(ivec3 c, vec3 rgb) {
    if (any(lessThan(c, ivec3(0))) || any(greaterThanEqual(c, ivec3(uResolution)))) return;
    uvec3 q = uvec3(rgb * uFixedScale + 0.5);
    if (q.r > 0u) imageAtomicAdd(uRed, c, q.r);
    if (q.g > 0u) imageAtomicAdd(uGreen, c, q.g);
    if (q.b > 0u) imageAtomicAdd(uBlue, c, q.b);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uCount) return;
    vec3 radiance;
    if (!shadeParticle(points[i].posScalar, points[i].dirValue, radiance)) return;

    // A Gaussian sprite of diameter s integrates to ≈ 0.193 s² × its peak radiance.
    vec3 luminosity = radiance * (0.193 * uPointSize * uPointSize);

    vec3 g = (points[i].posScalar.xyz - uVolumeMin) / uVolumeSize * float(uResolution) - 0.5;
    ivec3 c0 = ivec3(floor(g));
    vec3 f = g - vec3(c0);
    for (int k = 0; k < 8; ++k) {
        ivec3 o = ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        vec3 w3 = mix(1.0 - f, f, vec3(o));
        deposit(c0 + o, luminosity * (w3.x * w3.y * w3.z));
    }
}
