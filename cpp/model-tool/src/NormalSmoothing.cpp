#include "NormalSmoothing.hpp"

#include <cmath>
#include <map>
#include <tuple>
#include <utility>

#include "PositionKey.hpp"

namespace modeltool {
namespace {

struct Accumulator {
  double x{0.0}, y{0.0}, z{0.0};
};

// `meshScope` is -1 when `groups` is real smoothing-group data (global across every mesh, so an
// authored group spanning a mesh split still smooths seamlessly) or the owning mesh's own index
// when falling back to per-mesh smoothing (so two meshes never merge, even at a shared position).
struct AccumKey {
  PositionKey position;
  int group;
  int meshScope;
  bool operator<(const AccumKey& other) const {
    return std::tie(position, group, meshScope) < std::tie(other.position, other.group, other.meshScope);
  }
};

}  // namespace

void recomputeNormals(ImportedModel& model, const std::vector<MeshTriangleGroups>* groups) {
  auto triangleGroup = [&](std::size_t meshIndex, std::size_t triangleIndex) {
    return groups != nullptr ? (*groups)[meshIndex].triangleGroup[triangleIndex] : 0;
  };
  auto meshScope = [&](std::size_t meshIndex) { return groups != nullptr ? -1 : static_cast<int>(meshIndex); };

  // Pass 1: accumulate every triangle's face normal (magnitude proportional to twice its area, so
  // larger triangles contribute more -- the same area-weighting AssImp's own GenSmoothNormals uses)
  // into each of its three corners' (position, group, scope) bucket.
  std::map<AccumKey, Accumulator> accum;
  for (std::size_t m = 0; m < model.meshes.size(); ++m) {
    const ImportedMesh& mesh = model.meshes[m];
    const std::size_t triangleCount = mesh.indices.size() / 3;
    for (std::size_t t = 0; t < triangleCount; ++t) {
      const ImportedVertex& a = mesh.vertices[mesh.indices[t * 3 + 0]];
      const ImportedVertex& b = mesh.vertices[mesh.indices[t * 3 + 1]];
      const ImportedVertex& c = mesh.vertices[mesh.indices[t * 3 + 2]];
      const double ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
      const double vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
      const double nx = uy * vz - uz * vy;
      const double ny = uz * vx - ux * vz;
      const double nz = ux * vy - uy * vx;
      const int group = triangleGroup(m, t);
      const int scope = meshScope(m);
      for (const ImportedVertex* vertex : {&a, &b, &c}) {
        Accumulator& sum = accum[AccumKey{quantizePosition(vertex->px, vertex->py, vertex->pz), group, scope}];
        sum.x += nx;
        sum.y += ny;
        sum.z += nz;
      }
    }
  }

  // Pass 2: rebuild each mesh's vertex/index arrays, splitting a vertex into one copy per distinct
  // (originalIndex, group) it's used under -- required because a single vertex can only ever carry
  // one normal, but a group-unaware position match (like pass 1's own accumulator key) can span
  // several groups at a hard edge. Splitting by (originalIndex, group) rather than by
  // (position, group) directly preserves any *other* per-vertex attribute (uv, colour) two
  // originally-distinct vertices at the same position already carried -- e.g. a UV seam -- while
  // still letting them end up with the identical (accumulated) normal pass 1 computed for their
  // shared position+group. A vertex never referenced by any triangle in `indices` is dropped (this
  // pass only ever visits vertices via triangle corners).
  for (std::size_t m = 0; m < model.meshes.size(); ++m) {
    ImportedMesh& mesh = model.meshes[m];
    const std::size_t triangleCount = mesh.indices.size() / 3;
    const int scope = meshScope(m);
    std::vector<ImportedVertex> newVertices;
    std::vector<std::uint32_t> newIndices;
    newVertices.reserve(mesh.vertices.size());
    newIndices.reserve(mesh.indices.size());
    std::map<std::pair<std::uint32_t, int>, std::uint32_t> splitCache;

    for (std::size_t t = 0; t < triangleCount; ++t) {
      const int group = triangleGroup(m, t);
      for (int corner = 0; corner < 3; ++corner) {
        const std::uint32_t originalIndex = mesh.indices[t * 3 + static_cast<std::size_t>(corner)];
        const auto splitKey = std::make_pair(originalIndex, group);
        const auto cached = splitCache.find(splitKey);
        if (cached != splitCache.end()) {
          newIndices.push_back(cached->second);
          continue;
        }
        ImportedVertex vertex = mesh.vertices[originalIndex];
        const auto found = accum.find(AccumKey{quantizePosition(vertex.px, vertex.py, vertex.pz), group, scope});
        if (found != accum.end()) {
          const double len = std::sqrt(found->second.x * found->second.x + found->second.y * found->second.y + found->second.z * found->second.z);
          if (len > 1e-12) {
            vertex.nx = static_cast<float>(found->second.x / len);
            vertex.ny = static_cast<float>(found->second.y / len);
            vertex.nz = static_cast<float>(found->second.z / len);
          } else {
            // Every contributing triangle was degenerate (zero area) -- fall back to a safe,
            // non-NaN default rather than propagating garbage.
            vertex.nx = 0.0f;
            vertex.ny = 0.0f;
            vertex.nz = 1.0f;
          }
        }
        const auto newIndex = static_cast<std::uint32_t>(newVertices.size());
        newVertices.push_back(vertex);
        splitCache.emplace(splitKey, newIndex);
        newIndices.push_back(newIndex);
      }
    }

    mesh.vertices = std::move(newVertices);
    mesh.indices = std::move(newIndices);
  }
}

}  // namespace modeltool
