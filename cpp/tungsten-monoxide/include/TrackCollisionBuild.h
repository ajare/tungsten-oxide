#pragma once

// Track collision-BVH construction shared between Map::load() (the live game/editor host) and
// cpp/app/tools/mesh_physics_diag.cpp (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 6.0's headless
// diagnostic tool). Extracted out of Map.cpp so the diag tool can build the exact same
// TrackCollisionSurface a real Map::load() would, without depending on Map/ResourceException/the
// willpower resource system at all -- every function here throws plain std::runtime_error, and
// Map.cpp is the one place that catches those and rewraps them as ResourceException(this, ...) to
// keep its own error reporting unchanged.

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <mpp/ModelSerializer.h>

#include "Track.hpp"
#include "TrackCollision.hpp"

namespace mono {

// Throws std::runtime_error if `value` is empty, absolute, or escapes `root` via "..".
std::filesystem::path safeRelativePath(std::filesystem::path const& root, std::string const& value, char const* field);

// Decodes a little-endian float out of a raw mpp vertex-stream byte buffer at `offset`. Shared with
// Map.cpp's own render-mesh-append pass, which reads the same 36-byte layout for a purpose (GPU
// upload) outside this module's scope.
float readFloat(std::int8_t const* bytes, std::size_t offset);

// Placement local-to-world transforms (DRIVABLE_MESH_OBJECTS_PLAN.md's documented convention:
// scale, then yaw about Y / pitch about X / roll about Z, then translate). Shared between collision
// -triangle building here and Map.cpp's render-mesh-append pass, which must transform identically.
tox::Vec3 placementTransformPosition(tox::DrivableMeshObjectPlacementDefinition const& placement, tox::Vec3 const& local);
tox::Vec3 placementTransformNormal(tox::DrivableMeshObjectPlacementDefinition const& placement, tox::Vec3 const& localNormal);

// Every GeometryBatch id in `track.geometry` that must appear in a <TrackMeshes> selection --
// i.e. exactly the set buildCollisionTriangles below requires as `selectedNames`. Exposed so a
// caller with no Resources.xml to read (cpp/app/tools/mesh_physics_diag.cpp) can derive the same
// "collidable" set Map::load() gets from the editor's <TrackMeshes> export, rather than needing it
// supplied externally.
std::vector<std::string> collidableGeometryBatchIds(tox::Track const& track);

// The road's own collidable geometry (<TrackMeshes> selection), decoded from `serializer` and
// cross-checked against `track.geometry`. Throws std::runtime_error on any mismatch (missing/
// unexpected mesh name, non-triangular mesh, wrong vertex layout, geometry that doesn't match
// TrackData's own export).
std::vector<tox::CollisionTriangle> buildCollisionTriangles(
    mpp::ModelSerializer& serializer, tox::Track const& track, std::vector<std::string> const& selectedNames);

// Sentinel written to a mesh's indexStream slot when it has no index stream (a real, legitimate
// shape). ModelSerializer::getIndexData()/getIndexWidth() are undefined behavior to call on such a
// mesh; a caller must check the corresponding MeshObjectModel::indexStreamIds entry against this
// first and synthesize an identity index list itself when it matches, mirroring
// buildMeshObjectCollisionTriangles's own handling.
constexpr std::uint32_t kMeshObjectNoIndexStream = 0xFFFFFFFFu;

// One referenced drivable-mesh-object source file, loaded once and shared by every placement whose
// modelId names it.
struct MeshObjectModel {
  mpp::ModelSerializer serializer;
  std::vector<uint32_t> indexStreamIds;
};

std::shared_ptr<MeshObjectModel> loadMeshObjectModel(mpp::ResourceManager* resourceMgr, std::filesystem::path const& path);

// Every collidable sub-mesh's triangles, transformed into world space by its placement, across
// every placement in `track.definition.meshObjects`. `nextSurfaceId` is threaded through (not
// reset) so every triangle in the final BVH carries a unique id; `cache` persists across calls so a
// model referenced by several placements is only read from disk once.
std::vector<tox::CollisionTriangle> buildMeshObjectCollisionTriangles(
    tox::Track const& track, std::filesystem::path const& root, mpp::ResourceManager* resourceMgr, int& nextSurfaceId,
    std::map<std::string, std::shared_ptr<MeshObjectModel>>& cache);

}  // namespace mono
