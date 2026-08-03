#include "NormalSmoothing.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>

namespace modeltool {
namespace {

// Positions are quantized to a fixed grid before being used as a map key, so two vertices that are
// bit-for-bit-different floats but represent "the same" authored position (the common case at a
// sub-mesh boundary, where each mesh has its own independently-imported copy of the shared edge)
// still merge. 1e-4 world units is far finer than any real modeling seam, so this never merges two
// genuinely distinct positions in practice.
constexpr float kQuantizeScale = 10000.0f;

struct PositionKey {
  std::int64_t x, y, z;
  bool operator<(const PositionKey& other) const { return std::tie(x, y, z) < std::tie(other.x, other.y, other.z); }
};

PositionKey keyOf(const ImportedVertex& v) {
  return {std::llround(static_cast<double>(v.px) * kQuantizeScale), std::llround(static_cast<double>(v.py) * kQuantizeScale),
          std::llround(static_cast<double>(v.pz) * kQuantizeScale)};
}

struct Accumulator {
  double x{0.0}, y{0.0}, z{0.0};
};

}  // namespace

void recomputeSmoothNormalsAcrossMeshes(ImportedModel& model) {
  std::map<PositionKey, Accumulator> accum;

  // Pass 1: accumulate every triangle's face normal (magnitude proportional to twice its area, so
  // larger triangles contribute more -- the same area-weighting AssImp's own GenSmoothNormals
  // uses) into each of its three corners' shared-position bucket.
  for (const ImportedMesh& mesh : model.meshes) {
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
      const ImportedVertex& a = mesh.vertices[mesh.indices[t]];
      const ImportedVertex& b = mesh.vertices[mesh.indices[t + 1]];
      const ImportedVertex& c = mesh.vertices[mesh.indices[t + 2]];
      const double ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
      const double vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
      const double nx = uy * vz - uz * vy;
      const double ny = uz * vx - ux * vz;
      const double nz = ux * vy - uy * vx;
      for (const ImportedVertex* vertex : {&a, &b, &c}) {
        Accumulator& sum = accum[keyOf(*vertex)];
        sum.x += nx;
        sum.y += ny;
        sum.z += nz;
      }
    }
  }

  // Pass 2: write the normalized accumulated normal back into every vertex sharing that position,
  // across every mesh -- this is what makes the result continuous at a sub-mesh boundary, not just
  // within one mesh's own vertex array.
  for (ImportedMesh& mesh : model.meshes) {
    for (ImportedVertex& vertex : mesh.vertices) {
      const auto found = accum.find(keyOf(vertex));
      if (found == accum.end()) continue;
      const double len = std::sqrt(found->second.x * found->second.x + found->second.y * found->second.y + found->second.z * found->second.z);
      if (len <= 1e-12) continue;  // an isolated degenerate vertex (no contributing triangle area) -- leave its normal alone
      vertex.nx = static_cast<float>(found->second.x / len);
      vertex.ny = static_cast<float>(found->second.y / len);
      vertex.nz = static_cast<float>(found->second.z / len);
    }
  }
}

}  // namespace modeltool
