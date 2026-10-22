#include "io/VdbWriter.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <tuple>

#include "miniz.h"

namespace bh::vdb {

namespace {

// Layout as written by OpenVDB 10 (io/Archive, tree/RootNode, InternalNode,
// LeafNode, io/Compression): header, grid descriptors with stream offsets,
// then per grid: compression flags, metadata, transform, topology (root ->
// 32³ internal -> 16³ internal -> 8³ leaf masks), and the leaf values.
constexpr int64_t kMagic = 0x56444220;     // " BDV"
constexpr uint32_t kFileVersion = 224;     // OPENVDB_FILE_VERSION of OpenVDB 10
constexpr uint32_t kCompressZip = 0x1, kCompressActiveMask = 0x2;
constexpr uint8_t kNoMaskOrInactiveVals = 0;  // every inactive value is the background

class Out {
 public:
  void bytes(const void* p, size_t n) {
    const char* c = static_cast<const char*>(p);
    data_.insert(data_.end(), c, c + n);
  }
  template <typename T>
  void pod(T v) {
    bytes(&v, sizeof(T));
  }
  void string(const std::string& s) {
    pod(static_cast<uint32_t>(s.size()));
    bytes(s.data(), s.size());
  }
  void vec3d(double x, double y, double z) {
    pod(x);
    pod(y);
    pod(z);
  }
  size_t pos() const { return data_.size(); }
  template <typename T>
  void patch(size_t at, T v) {
    std::memcpy(data_.data() + at, &v, sizeof(T));
  }
  const std::vector<char>& data() const { return data_; }

 private:
  std::vector<char> data_;
};

// io::zipToStream: int64 size and zlib data, or -size and the raw bytes when
// compression does not pay off (and 0 for no data).
void zipped(Out& out, const void* data, size_t bytes) {
  if (bytes > 0) {
    mz_ulong packedSize = mz_compressBound(static_cast<mz_ulong>(bytes));
    std::vector<unsigned char> packed(packedSize);
    if (mz_compress2(packed.data(), &packedSize, static_cast<const unsigned char*>(data),
                     static_cast<mz_ulong>(bytes), 6) == MZ_OK &&
        packedSize < bytes) {
      out.pod(static_cast<int64_t>(packedSize));
      out.bytes(packed.data(), packedSize);
      return;
    }
  }
  out.pod(-static_cast<int64_t>(bytes));
  out.bytes(data, bytes);
}

struct Coord {
  int x, y, z;
  bool operator<(const Coord& o) const { return std::tie(x, y, z) < std::tie(o.x, o.y, o.z); }
};

struct Leaf {
  uint64_t mask[8] = {};      // 512 voxels, bit n = voxel ((n >> 6), (n >> 3) & 7, n & 7)
  std::vector<float> active;  // active values in mask order
};
struct Internal16 {
  std::map<int, Leaf> leaves;  // by offset in the 16³ node
};
struct Internal32 {
  std::map<int, Internal16> children;  // by offset in the 32³ node
};
using Root = std::map<Coord, Internal32>;

Root buildTree(const Grid& grid, std::array<int, 3> dims, std::array<int, 3> origin) {
  const int components = grid.components;
  Root root;
  auto floor8 = [](int v) { return v & ~7; };
  for (int lx = floor8(origin[0]); lx < origin[0] + dims[0]; lx += 8) {
    for (int ly = floor8(origin[1]); ly < origin[1] + dims[1]; ly += 8) {
      for (int lz = floor8(origin[2]); lz < origin[2] + dims[2]; lz += 8) {
        Leaf leaf;
        for (int a = 0; a < 8; ++a) {
          const int i = lx + a - origin[0];
          if (i < 0 || i >= dims[0]) continue;
          for (int b = 0; b < 8; ++b) {
            const int j = ly + b - origin[1];
            if (j < 0 || j >= dims[1]) continue;
            for (int c = 0; c < 8; ++c) {
              const int k = lz + c - origin[2];
              if (k < 0 || k >= dims[2]) continue;
              const float* v = &grid.values[((static_cast<size_t>(i) * dims[1] + j) * dims[2] + k) * components];
              bool on = false;
              for (int e = 0; e < components; ++e) on |= v[e] != 0.0f;
              if (!on) continue;
              const int n = (a << 6) | (b << 3) | c;  // ascending, as the mask iterates
              leaf.mask[n >> 6] |= uint64_t(1) << (n & 63);
              leaf.active.insert(leaf.active.end(), v, v + components);
            }
          }
        }
        if (leaf.active.empty()) continue;
        const Coord top{lx & ~4095, ly & ~4095, lz & ~4095};
        const int offset32 = (((lx & 4095) >> 7) << 10) | (((ly & 4095) >> 7) << 5) | ((lz & 4095) >> 7);
        const int offset16 = (((lx & 127) >> 3) << 8) | (((ly & 127) >> 3) << 4) | ((lz & 127) >> 3);
        root[top].children[offset32].leaves[offset16] = std::move(leaf);
      }
    }
  }
  return root;
}

void writeMask(Out& out, const std::vector<int>& onBits, int bits) {
  std::vector<uint64_t> words(static_cast<size_t>(bits / 64), 0);
  for (int n : onBits) words[static_cast<size_t>(n >> 6)] |= uint64_t(1) << (n & 63);
  out.bytes(words.data(), words.size() * sizeof(uint64_t));
}

// An internal node without tiles: child mask, empty value mask, then its
// (zero) active values under active-mask + ZIP compression.
template <typename Map>
void writeInternal(Out& out, const Map& children, int bits) {
  std::vector<int> offsets;
  for (const auto& child : children) offsets.push_back(child.first);
  writeMask(out, offsets, bits);
  writeMask(out, {}, bits);
  out.pod(kNoMaskOrInactiveVals);
  zipped(out, nullptr, 0);
}

std::string makeUuid() {
  std::random_device rd;
  std::mt19937_64 rng((static_cast<uint64_t>(rd()) << 32) ^ rd());
  char text[37];
  const uint64_t a = rng(), b = rng();
  std::snprintf(text, sizeof(text), "%08x-%04x-4%03x-%04x-%012llx", static_cast<unsigned>(a >> 32),
                static_cast<unsigned>(a >> 16) & 0xFFFF, static_cast<unsigned>(a) & 0xFFF,
                0x8000u | (static_cast<unsigned>(b >> 48) & 0x3FFF),
                static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFull));
  return std::string(text, 36);
}

}  // namespace

bool writeFile(const std::string& path, const std::vector<Grid>& grids, std::array<int, 3> dims,
               std::array<int, 3> origin, const Transform& transform, std::string* error) {
  auto fail = [&](const std::string& message) {
    if (error) *error = message;
    return false;
  };
  const size_t voxels = static_cast<size_t>(dims[0]) * dims[1] * dims[2];
  for (const Grid& g : grids) {
    if ((g.components != 1 && g.components != 3) || g.values.size() != voxels * g.components)
      return fail("grid '" + g.name + "': wrong size or component count");
  }

  Out out;
  out.pod(kMagic);
  out.pod(kFileVersion);
  out.pod(uint32_t(10));  // library version 10.0
  out.pod(uint32_t(0));
  out.pod(uint8_t(1));    // has grid offsets
  const std::string uuid = makeUuid();
  out.bytes(uuid.data(), 36);
  out.pod(uint32_t(0));   // no file metadata
  out.pod(static_cast<uint32_t>(grids.size()));

  const double v = transform.voxelSize;
  for (const Grid& g : grids) {
    const Root root = buildTree(g, dims, origin);

    out.string(g.name);
    out.string(g.components == 1 ? "Tree_float_5_4_3" : "Tree_vec3s_5_4_3");
    out.string("");  // instance parent
    const size_t offsets = out.pos();
    out.pod(int64_t(0));  // grid, block and end positions, patched below
    out.pod(int64_t(0));
    out.pod(int64_t(0));
    out.patch(offsets, static_cast<int64_t>(out.pos()));

    out.pod(kCompressZip | kCompressActiveMask);
    out.pod(uint32_t(g.components == 1 ? 4 : 3));  // metadata count
    if (g.components == 1) {
      out.string("class");
      out.string("string");
      out.string("fog volume");  // a string's metadata size is its length
    }
    out.string("name");
    out.string("string");
    out.string(g.name);
    out.string("file_bbox_min");
    out.string("vec3i");
    out.pod(uint32_t(12));
    out.pod(origin[0]);
    out.pod(origin[1]);
    out.pod(origin[2]);
    out.string("file_bbox_max");
    out.string("vec3i");
    out.pod(uint32_t(12));
    out.pod(origin[0] + dims[0] - 1);
    out.pod(origin[1] + dims[1] - 1);
    out.pod(origin[2] + dims[2] - 1);

    out.string("UniformScaleTranslateMap");
    out.vec3d(transform.translation[0], transform.translation[1], transform.translation[2]);
    out.vec3d(v, v, v);                                      // scale
    out.vec3d(v, v, v);                                      // voxel size
    out.vec3d(1.0 / v, 1.0 / v, 1.0 / v);                    // inverse scale
    out.vec3d(1.0 / (v * v), 1.0 / (v * v), 1.0 / (v * v));  // inverse scale squared
    out.vec3d(0.5 / v, 0.5 / v, 0.5 / v);                    // inverse twice scale

    // Topology.
    out.pod(int32_t(1));  // buffer count
    for (int e = 0; e < g.components; ++e) out.pod(0.0f);  // background
    out.pod(uint32_t(0));                                 // tiles
    out.pod(static_cast<uint32_t>(root.size()));          // children
    for (const auto& [coord, top] : root) {
      out.pod(coord.x);
      out.pod(coord.y);
      out.pod(coord.z);
      writeInternal(out, top.children, 32 * 32 * 32);
      for (const auto& [o32, mid] : top.children) {
        writeInternal(out, mid.leaves, 16 * 16 * 16);
        for (const auto& [o16, leaf] : mid.leaves) out.bytes(leaf.mask, sizeof(leaf.mask));
      }
    }
    out.patch(offsets + 8, static_cast<int64_t>(out.pos()));

    // Leaf values, in the same order.
    for (const auto& [coord, top] : root) {
      for (const auto& [o32, mid] : top.children) {
        for (const auto& [o16, leaf] : mid.leaves) {
          out.bytes(leaf.mask, sizeof(leaf.mask));
          out.pod(kNoMaskOrInactiveVals);
          zipped(out, leaf.active.data(), leaf.active.size() * sizeof(float));
        }
      }
    }
    out.patch(offsets + 16, static_cast<int64_t>(out.pos()));
  }

  const std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
  }
  std::ofstream file(path, std::ios::binary);
  if (!file) return fail("cannot open " + path);
  file.write(out.data().data(), static_cast<std::streamsize>(out.data().size()));
  return file.good() ? true : fail("write failed: " + path);
}

}  // namespace bh::vdb
