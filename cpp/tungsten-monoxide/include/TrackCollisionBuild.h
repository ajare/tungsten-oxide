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

// A Track resource's <Models> list (TRACK_MODEL_LIST_PLAN.md), parsed independently of
// cpp/model-xml (which this DLL deliberately doesn't link -- see that plan's architecture notes on
// why the editor/model-tool's TinyXML2-based fragment schema and this host's own wp::XmlNode-based
// parsing stay two separate implementations of the same documented format). `EmbeddedModelRef` is
// the minimal slice this module needs: enough to resolve a placement's `modelId` (which now names
// an embedded <Model id>, not a raw path) to the .mppmodel it references, and each of that model's
// meshes' own Type/Visible metadata -- superseding cpp/model-tool's old CollidableFlag.hpp
// name-suffix convention entirely (Milestone 7).
enum class ModelMeshType { Track,
                           Physical,
                           Decorative };

struct ModelMeshMeta {
  std::string name;
  ModelMeshType type{ModelMeshType::Physical};
  bool visible{true};
};

struct EmbeddedModelRef {
  std::string id;
  std::string modelFileReference;  // <ModelFile> text, relative to the Track resource's own directory
  std::vector<ModelMeshMeta> meshes;
};

// Resolves a placement's `modelId` to a relative ModelFile reference by looking it up in
// `embeddedModels` by id. Falls back to treating `modelId` as the reference itself when no match is
// found (including when `embeddedModels` is empty) -- this is what lets
// cpp/app/tools/mesh_physics_diag.cpp keep working unchanged: it builds a collision surface straight
// from a CLI-supplied TrackData JSON with no Resources XML/<Models> list to parse at all, so its
// placements' `modelId` is still a direct relative path, exactly as it always was.
std::string resolveModelFileReference(std::string const& modelId, std::vector<EmbeddedModelRef> const& embeddedModels);

// Looks up one mesh's own Type/Visible metadata by (placement modelId, mesh name). Returns nullptr
// when no embedded Model matches `modelId` at all (same CLI-tool fallback as above) OR when the
// Model is found but doesn't mention this particular mesh name -- callers default an absent result
// to Physical/visible, matching model-tool's own "no XML metadata yet" default.
ModelMeshMeta const* findMeshMeta(std::string const& modelId, std::string const& meshName, std::vector<EmbeddedModelRef> const& embeddedModels);

// Throws std::runtime_error if `value` is empty, absolute, or escapes `root` via "..".
std::filesystem::path safeRelativePath(std::filesystem::path const& root, std::string const& value, char const* field);

// Decodes a little-endian float out of a raw mpp vertex-stream byte buffer at `offset`. Shared with
// Map.cpp's own render-mesh-append pass, which reads the same 36-byte layout for a purpose (GPU
// upload) outside this module's scope.
float readFloat(std::int8_t const* bytes, std::size_t offset);

// Placement local-to-world transforms (DRIVABLE_MESH_OBJECTS_PLAN.md's documented convention:
// scale, then yaw about Y / pitch about X / roll about Z, then translate). Shared between collision
// -triangle building here and Map.cpp's render-mesh-append pass, which must transform identically.
tox::Vec3 placementTransformPosition(tox::ModelPlacementDefinition const& placement, tox::Vec3 const& local);
tox::Vec3 placementTransformNormal(tox::ModelPlacementDefinition const& placement, tox::Vec3 const& localNormal);

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
  // The resolved file this was loaded from -- Map.cpp's render-mesh-append pass needs it again
  // (to declare/rebase an embedded material, docs/GLTF_IMPORT_PLAN.md M4), and re-deriving it from
  // `placement.modelId` a second time there would require re-running resolveModelFileReference/
  // safeRelativePath against a cache keyed by modelId, not by the resolved path.
  std::filesystem::path path;
};

std::shared_ptr<MeshObjectModel> loadMeshObjectModel(mpp::ResourceManager* resourceMgr, std::filesystem::path const& path);

// Every Type=Physical sub-mesh's triangles (Type=Decorative is excluded -- render-only, matching
// the old CollidableFlag.hpp decorative marker it replaces; Type=Track is never expected on a
// placement, only the primary model), transformed into world space by its placement, across every
// placement in `track.definition.meshObjects`. `nextSurfaceId` is threaded through (not reset) so
// every triangle in the final BVH carries a unique id; `cache` persists across calls so a model
// referenced by several placements is only read from disk once. `embeddedModels` resolves each
// placement's `modelId` and its meshes' Type -- see resolveModelFileReference/findMeshMeta above for
// the fallback when it's empty (mesh_physics_diag.cpp).
std::vector<tox::CollisionTriangle> buildMeshObjectCollisionTriangles(
    tox::Track const& track, std::filesystem::path const& root, mpp::ResourceManager* resourceMgr, int& nextSurfaceId,
    std::map<std::string, std::shared_ptr<MeshObjectModel>>& cache, std::vector<EmbeddedModelRef> const& embeddedModels = {});

}  // namespace mono
