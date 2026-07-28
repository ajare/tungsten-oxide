// TrackMesh.cpp — adapt normalized schema topology through Willpower.Geometry,
// then retain double-precision world loops, triangles, rails and render batches.
#include "TrackMesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

#include "Track.hpp"
#include "willpower/geometry/Edge.h"
#include "willpower/geometry/Mesh.h"
#include "willpower/geometry/Polygon.h"
#include "willpower/geometry/Vertex.h"

namespace tox {
namespace {

bool pointInLoop(const std::vector<Vec2d>& loop, double x, double z) {
  if (loop.size() < 3) return false;
  bool inside = false;
  for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
    const Vec2d& a = loop[i];
    const Vec2d& b = loop[j];
    if ((a.y > z) != (b.y > z) && x < (b.x - a.x) * (z - a.y) / (b.y - a.y) + a.x) inside = !inside;
  }
  return inside;
}

Vec2d transform(const MeshPlacementDefinition& placement, const MeshVertexDefinition& vertex) {
  const double angle = placement.rotation * 3.14159265358979323846 / 180.0;
  const double cosine = std::cos(angle), sine = std::sin(angle);
  return {vertex.x * cosine - vertex.y * sine + placement.x,
          vertex.x * sine + vertex.y * cosine + placement.z};
}

Vec3 normalOf(const Vec3& a, const Vec3& b, const Vec3& c) {
  Vec3 normal;
  normal.crossVectors(b.clone().sub(a), c.clone().sub(a)).normalize();
  return normal;
}

void addTriangle(GeometryBatch& batch, const Vec3& a, const Vec3& b, const Vec3& c) {
  const Vec3 normal = normalOf(a, b, c);
  const std::uint32_t base = static_cast<std::uint32_t>(batch.vertices.size());
  batch.vertices.push_back({a, normal, {}, {}});
  batch.vertices.push_back({b, normal, {}, {}});
  batch.vertices.push_back({c, normal, {}, {}});
  batch.indices.insert(batch.indices.end(), {base, base + 1, base + 2});
}

struct AssetTopology {
  wp::geometry::Mesh mesh;
  std::map<int, std::uint32_t> vertices, edges, polygons;
  std::map<std::uint32_t, const MeshVertexDefinition*> vertexByIndex;
};

AssetTopology buildTopology(const MeshAssetDefinition& asset) {
  AssetTopology topology;
  for (const auto& vertex : asset.vertices) {
    if (topology.vertices.contains(vertex.id)) throw std::runtime_error("duplicate vertex id");
    const std::uint32_t index = topology.mesh.addVertex(wp::geometry::Vertex(static_cast<float>(vertex.x), static_cast<float>(vertex.y)));
    topology.vertices[vertex.id] = index;
    topology.vertexByIndex[index] = &vertex;
  }
  for (const auto& edge : asset.edges) {
    if (topology.edges.contains(edge.id) || !topology.vertices.contains(edge.vertex0) || !topology.vertices.contains(edge.vertex1))
      throw std::runtime_error("invalid or duplicate edge id");
    topology.edges[edge.id] = topology.mesh.addEdge(
        wp::geometry::Edge(topology.vertices.at(edge.vertex0), topology.vertices.at(edge.vertex1)));
  }
  for (const auto& polygon : asset.polygons) {
    if (topology.polygons.contains(polygon.id) || polygon.edges.size() < 3) throw std::runtime_error("invalid or duplicate polygon id");
    std::vector<std::uint32_t> directed;
    directed.reserve(polygon.edges.size() * 3);
    for (const auto& edge : polygon.edges) {
      if (!topology.vertices.contains(edge.v0) || !topology.vertices.contains(edge.v1) || !topology.edges.contains(edge.edge))
        throw std::runtime_error("polygon references missing topology");
      directed.insert(directed.end(), {topology.vertices.at(edge.v0), topology.vertices.at(edge.v1), topology.edges.at(edge.edge)});
    }
    topology.polygons[polygon.id] = topology.mesh.addPolygon(wp::geometry::Polygon(directed));
  }
  for (const auto& polygon : asset.polygons) {
    for (int hole : polygon.holes) {
      if (!topology.polygons.contains(hole)) throw std::runtime_error("polygon references missing hole");
      topology.mesh.addHoleToPolygon(topology.polygons.at(polygon.id), topology.polygons.at(hole));
    }
  }
  return topology;
}

std::vector<Vec2d> worldLoop(const MeshPolygonDefinition& polygon, const MeshAssetDefinition& asset,
                             const MeshPlacementDefinition& placement) {
  std::map<int, const MeshVertexDefinition*> vertices;
  for (const auto& vertex : asset.vertices) vertices[vertex.id] = &vertex;
  std::vector<Vec2d> loop;
  loop.reserve(polygon.edges.size());
  for (const auto& edge : polygon.edges) {
    if (!vertices.contains(edge.v0)) throw std::runtime_error("polygon loop references missing vertex");
    loop.push_back(transform(placement, *vertices.at(edge.v0)));
  }
  return loop;
}

MeshRegion compilePlacement(const MeshAssetDefinition& asset, const AssetTopology& topology,
                            const MeshPlacementDefinition& placement) {
  MeshRegion region;
  region.id = placement.id;
  region.assetId = asset.id;
  region.elevation = placement.elevation;
  region.railHeight = asset.railHeight;
  region.bounds = {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                   std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

  std::map<int, const MeshPolygonDefinition*> polygons;
  for (const auto& polygon : asset.polygons) polygons[polygon.id] = &polygon;
  for (const auto& polygon : asset.polygons) {
    if (polygon.hole) continue;
    MeshPolygon compiled;
    compiled.polygonId = polygon.id;
    compiled.outer = worldLoop(polygon, asset, placement);
    for (int holeId : polygon.holes)
      if (polygons.contains(holeId)) compiled.holes.push_back(worldLoop(*polygons.at(holeId), asset, placement));
    for (const Vec2d& point : compiled.outer) {
      region.bounds.minX = std::min(region.bounds.minX, point.x);
      region.bounds.maxX = std::max(region.bounds.maxX, point.x);
      region.bounds.minZ = std::min(region.bounds.minZ, point.y);
      region.bounds.maxZ = std::max(region.bounds.maxZ, point.y);
    }
    region.polygons.push_back(std::move(compiled));

    const auto& wpPolygon = topology.mesh.getPolygon(topology.polygons.at(polygon.id));
    for (std::size_t triangle = 0; triangle < wpPolygon.getTriangulationTriangleCount(); ++triangle) {
      std::uint32_t a, b, c;
      wpPolygon.getTriangulationVertexIndices(triangle, a, b, c);
      region.triangles.push_back({{transform(placement, *topology.vertexByIndex.at(a)),
                                   transform(placement, *topology.vertexByIndex.at(b)),
                                   transform(placement, *topology.vertexByIndex.at(c))}});
    }
  }
  if (!std::isfinite(region.bounds.minX)) region.bounds = {};

  for (const auto& edge : asset.edges) {
    if (!edge.rail) continue;
    const Vec2d a = transform(placement, *topology.vertexByIndex.at(topology.vertices.at(edge.vertex0)));
    const Vec2d b = transform(placement, *topology.vertexByIndex.at(topology.vertices.at(edge.vertex1)));
    const double dx = b.x - a.x, dz = b.y - a.y, length = std::hypot(dx, dz);
    if (length < 1e-9) continue;
    double nx = dz / length, nz = -dx / length;
    const double mx = (a.x + b.x) / 2, mz = (a.y + b.y) / 2;
    if (region.contains(mx + nx * 0.01, mz + nz * 0.01)) {
      nx = -nx;
      nz = -nz;
    }
    region.rails.push_back({edge.id, a, b, nx, nz, length});
  }
  return region;
}

void addGeometry(Track& track, const MeshRegion& region) {
  GeometryBatch surface;
  surface.id = "mesh-" + region.id + "-surface";
  // Fixed material for every mesh region -- must stay in sync with
  // cpp/tungsten-monoxide/resources/Resources.xml's Namespace="Tracks" Material
  // "DefaultMeshMaterial", and with MaterialCatalog's startup existence check for it.
  surface.materialKey = "Tracks/DefaultMeshMaterial";
  surface.kind = GeometryKind::MeshSurface;
  for (const auto& triangle : region.triangles) {
    addTriangle(surface, {triangle.points[0].x, region.elevation, triangle.points[0].y},
                {triangle.points[1].x, region.elevation, triangle.points[1].y},
                {triangle.points[2].x, region.elevation, triangle.points[2].y});
  }
  track.geometry.push_back(std::move(surface));

  GeometryBatch rails;
  rails.id = "mesh-" + region.id + "-rails";
  // Same fixed rail material as PathRail (see TrackBake.cpp's pathGeometry) -- rails always use
  // "Tracks/DefaultRailMaterial" regardless of what mesh region they belong to.
  rails.materialKey = "Tracks/DefaultRailMaterial";
  rails.kind = GeometryKind::MeshRail;
  for (const auto& rail : region.rails) {
    const Vec3 a(rail.a.x, region.elevation, rail.a.y), b(rail.b.x, region.elevation, rail.b.y);
    const Vec3 at(rail.a.x, region.elevation + region.railHeight, rail.a.y);
    const Vec3 bt(rail.b.x, region.elevation + region.railHeight, rail.b.y);
    addTriangle(rails, a, b, bt);
    addTriangle(rails, a, bt, at);
  }
  track.geometry.push_back(std::move(rails));
}

}  // namespace

bool MeshRegion::contains(double x, double z) const {
  for (const auto& polygon : polygons) {
    if (!pointInLoop(polygon.outer, x, z)) continue;
    if (std::any_of(polygon.holes.begin(), polygon.holes.end(), [&](const auto& hole) { return pointInLoop(hole, x, z); })) continue;
    return true;
  }
  return false;
}

bool MeshRegion::withinBounds(double x, double z, double padding) const {
  return x >= bounds.minX - padding && x <= bounds.maxX + padding && z >= bounds.minZ - padding && z <= bounds.maxZ + padding;
}

std::optional<double> segmentCrossing(const Vec2d& a, const Vec2d& b, const Vec2d& c, const Vec2d& d) {
  const double rx = b.x - a.x, rz = b.y - a.y;
  const double sx = d.x - c.x, sz = d.y - c.y;
  const double denominator = rx * sz - rz * sx;
  if (std::fabs(denominator) < 1e-12) return std::nullopt;
  const double t = ((c.x - a.x) * sz - (c.y - a.y) * sx) / denominator;
  const double u = ((c.x - a.x) * rz - (c.y - a.y) * rx) / denominator;
  if (t < 0 || t > 1 || u < 0 || u > 1) return std::nullopt;
  return t;
}

MeshMoveResult slideAlongRails(const MeshRegion& region, const Vec2d& from, const Vec2d& to, Vec2d& velocity,
                               double margin, double restitution) {
  Vec2d current = from, target = to;
  bool hit = false;
  for (int iteration = 0; iteration < 3; ++iteration) {
    const MeshRail* nearest = nullptr;
    double nearestT = 0;
    for (const auto& rail : region.rails) {
      const auto t = segmentCrossing(current, target, rail.a, rail.b);
      if (t && (!nearest || *t < nearestT)) {
        nearest = &rail;
        nearestT = *t;
      }
    }
    if (!nearest) break;
    hit = true;
    const double directionX = target.x - current.x, directionZ = target.y - current.y;
    const double side = directionX * nearest->nx + directionZ * nearest->nz >= 0 ? 1.0 : -1.0;
    const double hitX = current.x + directionX * nearestT, hitZ = current.y + directionZ * nearestT;
    current = {hitX - nearest->nx * margin * side, hitZ - nearest->nz * margin * side};
    const double remainderX = target.x - hitX, remainderZ = target.y - hitZ;
    const double into = remainderX * nearest->nx + remainderZ * nearest->nz;
    target = {current.x + remainderX - nearest->nx * into,
              current.y + remainderZ - nearest->nz * into};
    const double velocityInto = velocity.x * nearest->nx + velocity.y * nearest->nz;
    if (velocityInto * side > 0) {
      const double impulse = velocityInto * (1 + restitution);
      velocity.x -= nearest->nx * impulse;
      velocity.y -= nearest->nz * impulse;
    }
  }
  return {target.x, target.y, hit};
}

void compileTrackMeshes(Track& track, std::vector<TrackWarning>& warnings) {
  track.meshRegions.clear();
  std::map<std::string, std::unique_ptr<AssetTopology>> topologies;
  std::set<std::string> failedAssets;
  for (const auto& placement : track.definition.meshes) {
    const auto asset = track.definition.meshAssets.find(placement.assetId);
    if (asset == track.definition.meshAssets.end() || failedAssets.contains(placement.assetId)) continue;
    try {
      if (!topologies.contains(placement.assetId))
        topologies[placement.assetId] = std::make_unique<AssetTopology>(buildTopology(asset->second));
      MeshRegion region = compilePlacement(asset->second, *topologies.at(placement.assetId), placement);
      addGeometry(track, region);
      track.meshRegions.push_back(std::move(region));
    } catch (const std::exception& error) {
      failedAssets.insert(placement.assetId);
      warnings.push_back({"mesh-compile-failed", error.what(), placement.id});
    } catch (...) {
      failedAssets.insert(placement.assetId);
      warnings.push_back({"mesh-compile-failed", "unknown Willpower topology error", placement.id});
    }
  }
}

}  // namespace tox
