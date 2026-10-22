#pragma once

#include <array>
#include <string>
#include <vector>

// A small, dependency-free OpenVDB file writer (no Boost/TBB/Blosc): sparse
// grids of floats or float triples (vec3s), saved in the format OpenVDB 10
// writes (file version 224, the 5-4-3 tree, active-mask + ZIP compression), so
// Blender, Houdini and OpenVDB itself read them. Voxels equal to zero are
// left out (inactive background).
namespace bh::vdb {

struct Grid {
  std::string name;           // e.g. "density" or "color"
  int components = 1;         // 1: float grid, 3: vec3s grid
  // Dense block of size dims, `components` floats per voxel, the last index
  // fastest: value(i, j, k) at ((i * dims[1] + j) * dims[2] + k) * components.
  std::vector<float> values;
};

struct Transform {
  double voxelSize = 1.0;
  std::array<double, 3> translation{0.0, 0.0, 0.0};  // world position of index (0, 0, 0)
};

// Writes `grids` (all of size dims, placed at index coordinates starting at
// `origin`) to `path`. Returns false and sets `error` on failure.
bool writeFile(const std::string& path, const std::vector<Grid>& grids, std::array<int, 3> dims,
               std::array<int, 3> origin, const Transform& transform, std::string* error = nullptr);

}  // namespace bh::vdb
