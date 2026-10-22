// Fixed-point light sums -> emissivity (light per unit volume) in a filterable texture.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(r32ui, binding = 0) uniform readonly uimage3D uRed;
layout(r32ui, binding = 1) uniform readonly uimage3D uGreen;
layout(r32ui, binding = 2) uniform readonly uimage3D uBlue;
layout(rgba16f, binding = 3) uniform writeonly image3D uEmission;

uniform int uResolution;
uniform float uInvScaleVolume;  // 1 / (fixed-point scale × voxel volume)

void main() {
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, ivec3(uResolution)))) return;
    vec3 sum = vec3(imageLoad(uRed, c).r, imageLoad(uGreen, c).r, imageLoad(uBlue, c).r);
    imageStore(uEmission, c, vec4(sum * uInvScaleVolume, 0.0));
}
