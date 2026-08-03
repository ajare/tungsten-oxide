#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include <mpp/ModelSerializer.h>
#include <mpp/ProgrammaticModelStream.h>

#include <utils/FileSystem.h>

#include <applib/TrackMaterial.h>

#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/MaterialResource.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include "Map.h"
#include "Simulation.hpp"
#include "StartGrid.hpp"

using namespace std;
using namespace wp;

namespace {

mpp::mesh::MeshSpecification trackMeshSpecification() {
  mpp::mesh::MeshSpecification meshSpec(mpp::mesh::Primitive::Type::Triangles);
  auto attribLayout = meshSpec.createVertexBufferAttributeLayout(false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Position3, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Normal3, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::TexCoord2, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  meshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  meshSpec.setIndexedVertices(false);
  return meshSpec;
}

string resolveMaterialMppName(Map* map, string const& materialKey) {
  auto dependent = map->getDependentResource(materialKey);
  if (dependent->getType() == "TrackMaterial")
    return static_cast<applib::TrackMaterial*>(dependent.get())->getMaterial()->getQualifiedName();
  if (dependent->getType() == "Material") return dependent->getQualifiedName();
  throw application::resourcesystem::ResourceException(
      map, "material '" + materialKey + "' is a '" + dependent->getType() + "' resource, expected TrackMaterial or Material.");
}

filesystem::path safeRelativePath(Map* map, filesystem::path const& root, string const& value, char const* field) {
  filesystem::path relative(value);
  if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
    throw application::resourcesystem::ResourceException(map, string(field) + " must be a non-empty relative path.");
  for (auto const& part : relative)
    if (part == "..")
      throw application::resourcesystem::ResourceException(map, string(field) + " may not traverse outside the resource directory.");
  return (root / relative).lexically_normal();
}

bool gameplayKind(tox::GeometryKind kind) {
  // Everything a ship can physically contact -- drivable surfaces, reservation walls, and both
  // Path- and MeshRegion-authored rails -- goes into the collision BVH. PathShell stays out: it's
  // render-only. Must stay in lock-step with the editor's <TrackMeshes> export loop
  // (MppModelExport.cpp) -- same set, same reasoning.
  return kind == tox::GeometryKind::PathSurface || kind == tox::GeometryKind::MeshSurface ||
         kind == tox::GeometryKind::ReservationWall || kind == tox::GeometryKind::PathRail ||
         kind == tox::GeometryKind::MeshRail;
}

float readFloat(int8_t const* bytes, size_t offset) {
  float value;
  memcpy(&value, bytes + offset, sizeof(value));
  return value;
}

bool matchesExportedFloat(double expected, double actual) {
  return static_cast<double>(static_cast<float>(expected)) == actual;
}

vector<tox::CollisionTriangle> buildCollisionTriangles(
    Map* map, mpp::ModelSerializer& serializer, tox::Track const& track, vector<string> const& selectedNames) {
  set<string> selected(selectedNames.begin(), selectedNames.end());
  set<string> expectedNames;
  std::map<string, tox::GeometryBatch const*> expectedByName;
  for (auto const& batch : track.geometry) {
    if (!gameplayKind(batch.kind)) continue;
    expectedNames.insert(batch.id);
    expectedByName.emplace(batch.id, &batch);
  }
  if (selected != expectedNames) {
    string detail = "TrackMeshes must contain exactly every collidable geometry batch "
                     "(PathSurface, MeshSurface, ReservationWall, PathRail, MeshRail)";
    for (auto const& name : expectedNames)
      if (!selected.count(name)) detail += "; missing '" + name + "'";
    for (auto const& name : selected)
      if (!expectedNames.count(name)) detail += "; unexpected '" + name + "'";
    throw application::resourcesystem::ResourceException(map, detail + ".");
  }

  std::map<string, size_t> modelByName;
  for (size_t i = 0; i < serializer.getMeshCount(); ++i) {
    string const& name = serializer.getName(i);
    if (!modelByName.emplace(name, i).second)
      throw application::resourcesystem::ResourceException(map, "model contains duplicate mesh name '" + name + "'.");
  }

  vector<tox::CollisionTriangle> triangles;
  int surfaceId = 0;
  for (string const& name : selectedNames) {
    auto modelIt = modelByName.find(name);
    if (modelIt == modelByName.end())
      throw application::resourcesystem::ResourceException(map, "listed track mesh '" + name + "' is missing from ModelFile.");
    size_t meshIndex = modelIt->second;
    if (serializer.getPrimitiveType(meshIndex) != mpp::mesh::Primitive::Type::Triangles)
      throw application::resourcesystem::ResourceException(map, "listed track mesh '" + name + "' is not triangular.");

    size_t vertexCount, stride;
    shared_ptr<const int8_t> data;
    serializer.getVertexStream(meshIndex, 0, &vertexCount, &stride, &data);
    if (stride != 36 || vertexCount % 3 != 0)
      throw application::resourcesystem::ResourceException(
          map, "listed track mesh '" + name + "' must use the exported 36-byte non-indexed triangle layout.");

    auto const& expected = *expectedByName.at(name);
    if (expected.vertices.size() != vertexCount)
      throw application::resourcesystem::ResourceException(map, "listed track mesh '" + name + "' triangle count does not match TrackData.");

    vector<tox::RenderVertex> decoded;
    decoded.reserve(vertexCount);
    for (size_t v = 0; v < vertexCount; ++v) {
      auto bytes = data.get() + v * stride;
      tox::RenderVertex vertex;
      vertex.position = {readFloat(bytes, 0), readFloat(bytes, 4), readFloat(bytes, 8)};
      vertex.normal = {readFloat(bytes, 12), readFloat(bytes, 16), readFloat(bytes, 20)};
      if (glm::dot(vertex.normal, vertex.normal) < 1e-12)
        throw application::resourcesystem::ResourceException(
            map, "listed track mesh '" + name + "' contains an unusable vertex normal.");
      auto const& reference = expected.vertices[v];
      if (!matchesExportedFloat(reference.position.x, vertex.position.x) ||
          !matchesExportedFloat(reference.position.y, vertex.position.y) ||
          !matchesExportedFloat(reference.position.z, vertex.position.z) ||
          !matchesExportedFloat(reference.normal.x, vertex.normal.x) ||
          !matchesExportedFloat(reference.normal.y, vertex.normal.y) ||
          !matchesExportedFloat(reference.normal.z, vertex.normal.z))
        throw application::resourcesystem::ResourceException(
            map, "listed track mesh '" + name + "' vertex data does not match TrackData export geometry.");
      vertex.normal = tox::normalizeSafe(vertex.normal);
      decoded.push_back(vertex);
    }
    for (size_t v = 0; v < decoded.size(); v += 3) {
      tox::CollisionTriangle triangle;
      triangle.surfaceId = surfaceId;
      for (int corner = 0; corner < 3; ++corner) {
        triangle.positions[corner] = decoded[v + corner].position;
        triangle.normals[corner] = decoded[v + corner].normal;
      }
      triangles.push_back(triangle);
    }
    ++surfaceId;
  }
  return triangles;
}

// --- Drivable mesh object placements (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3) ---------------
//
// core never loads or compiles a placement's referenced .mppmodel -- Track::definition.meshObjects
// carries only authored data (modelId + 6-DOF transform), per the plan's "`.mppmodel` loading is
// host-only" architecture note. This is the one place it's actually resolved: `modelId` is a
// filename relative to the same directory-based resource location as the track's own ModelFile/
// TrackData (resolved via safeRelativePath, exactly like those two), not a declared
// <DependentResource> -- there's no need for the generic named-resource-dependency machinery
// materials use (that exists to support cross-Map GPU resource sharing/refcounting; a placement's
// model is loaded fresh per Map::load() instead, cached only within that one call by modelId so
// multiple placements sharing one model parse it once, matching the plan's own 3.3 wording).

// Mirrors cpp/model-tool/include/CollidableFlag.hpp's naming convention exactly, but reimplemented
// independently: model-tool and tungsten-monoxide share no code, only this file-format convention
// (same "two independent consumers, one documented format" precedent as gameplayKind()'s own
// lock-step comment with the editor's MppModelExport.cpp).
constexpr char kDecorativeMeshNameSuffix[] = "~decorative";
bool meshNameIsCollidable(string const& name) {
  size_t const suffixLen = sizeof(kDecorativeMeshNameSuffix) - 1;
  if (name.size() < suffixLen) return true;
  return name.compare(name.size() - suffixLen, suffixLen, kDecorativeMeshNameSuffix) != 0;
}

// Local-to-world position: scale, then rotate (yaw about Y, then pitch about X, then roll about Z
// -- DrivableMeshObjectPlacementDefinition's own documented convention), then translate.
tox::Vec3 placementTransformPosition(tox::DrivableMeshObjectPlacementDefinition const& placement, tox::Vec3 const& local) {
  tox::Vec3 scaled(local.x * placement.scale.x, local.y * placement.scale.y, local.z * placement.scale.z);
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  tox::Vec3 rotated = tox::applyAxisAngle(scaled, tox::Vec3(0.0, 1.0, 0.0), placement.rotation.x * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(1.0, 0.0, 0.0), placement.rotation.y * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(0.0, 0.0, 1.0), placement.rotation.z * kDegToRad);
  return rotated + placement.position;
}

// Local-to-world normal: the inverse-transpose of scale-then-rotate. Rotation is orthogonal (its
// own inverse-transpose); a diagonal scale matrix's inverse-transpose is just 1/scale component-
// wise -- so this divides by scale BEFORE rotating (the opposite order from position), then
// renormalizes to undo scale's effect on magnitude.
tox::Vec3 placementTransformNormal(tox::DrivableMeshObjectPlacementDefinition const& placement, tox::Vec3 const& localNormal) {
  tox::Vec3 unscaled(placement.scale.x != 0.0 ? localNormal.x / placement.scale.x : localNormal.x,
                     placement.scale.y != 0.0 ? localNormal.y / placement.scale.y : localNormal.y,
                     placement.scale.z != 0.0 ? localNormal.z / placement.scale.z : localNormal.z);
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  tox::Vec3 rotated = tox::applyAxisAngle(unscaled, tox::Vec3(0.0, 1.0, 0.0), placement.rotation.x * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(1.0, 0.0, 0.0), placement.rotation.y * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(0.0, 0.0, 1.0), placement.rotation.z * kDegToRad);
  return tox::normalizeSafe(rotated);
}

// A mesh written with no index stream (a real, legitimate shape -- see
// cpp/model-tool/src/MppModelImport.cpp's own extensive comment on this exact case, which this
// mirrors) makes ModelSerializer::getIndexData()/getIndexWidth() undefined behavior to call at
// all: there is no public accessor that safely exposes a mesh's raw indexStream id, so it has to
// be read directly off disk first, mirroring ModelSerializer::readHeader/readDirectory/readMesh
// byte-for-byte. In practice every placement model comes from model-tool, which always writes real
// indices (never this sentinel) -- this guard exists so a hand-crafted or differently-tooled file
// fails loudly with a clear error instead of risking a native crash.
constexpr uint32_t kMeshObjectNoIndexStream = 0xFFFFFFFFu;

uint32_t readRawU32(ifstream& fp, Map* map, string const& utf8Path) {
  uint32_t value = 0;
  fp.read(reinterpret_cast<char*>(&value), sizeof(value));
  if (!fp) throw application::resourcesystem::ResourceException(map, "unexpected end of file while reading mesh metadata from '" + utf8Path + "'.");
  return value;
}

void skipRawString(ifstream& fp, Map* map, string const& utf8Path) {
  uint32_t const len = readRawU32(fp, map, utf8Path);
  fp.seekg(static_cast<streamoff>(len), ios::cur);
  if (!fp) throw application::resourcesystem::ResourceException(map, "unexpected end of file while reading mesh metadata from '" + utf8Path + "'.");
}

vector<uint32_t> readMeshObjectIndexStreamIds(Map* map, string const& utf8Path, size_t meshCount) {
  ifstream fp(utf8Path, ios::binary);
  if (!fp) throw application::resourcesystem::ResourceException(map, "could not reopen '" + utf8Path + "' to inspect mesh metadata.");

  fp.seekg(12, ios::beg);  // Header: 4-byte magic + u16 + u16 + u32.
  uint32_t meshMetadataStart = 0;
  for (int entryIndex = 0; entryIndex < 6; ++entryIndex) {
    uint32_t const type = readRawU32(fp, map, utf8Path);
    uint32_t const startOffset = readRawU32(fp, map, utf8Path);
    readRawU32(fp, map, utf8Path);  // endOffset, unused
    readRawU32(fp, map, utf8Path);  // count, unused
    if (type == 5) meshMetadataStart = startOffset;  // Directory::Entry::Type::MeshMetadata
  }

  fp.seekg(meshMetadataStart, ios::beg);
  vector<uint32_t> indexStreamIds;
  indexStreamIds.reserve(meshCount);
  for (size_t i = 0; i < meshCount; ++i) {
    skipRawString(fp, map, utf8Path);  // name
    readRawU32(fp, map, utf8Path);      // primitiveType
    readRawU32(fp, map, utf8Path);      // primitiveCount
    skipRawString(fp, map, utf8Path);  // material
    uint32_t const numVertexBuffers = readRawU32(fp, map, utf8Path);
    for (uint32_t v = 0; v < numVertexBuffers; ++v) readRawU32(fp, map, utf8Path);  // vertex buffer ids
    indexStreamIds.push_back(readRawU32(fp, map, utf8Path));                        // indexStream
  }
  return indexStreamIds;
}

// One referenced drivable-mesh-object source file, loaded once and shared by every placement whose
// modelId names it.
struct MeshObjectModel {
  mpp::ModelSerializer serializer;
  vector<uint32_t> indexStreamIds;
};

shared_ptr<MeshObjectModel> loadMeshObjectModel(Map* map, mpp::ResourceManager* resourceMgr, filesystem::path const& path) {
  auto model = make_shared<MeshObjectModel>();
  model->serializer = mpp::ModelSerializer(resourceMgr);
  try {
    model->serializer.load(path.string());
  } catch (exception const& error) {
    throw application::resourcesystem::ResourceException(map, "failed to load drivable mesh object model '" + path.string() + "': " + error.what());
  }
  model->indexStreamIds = readMeshObjectIndexStreamIds(map, path.string(), model->serializer.getMeshCount());
  return model;
}

// Every collidable sub-mesh's triangles, transformed into world space by its placement, across
// every placement in `track.definition.meshObjects`. `nextSurfaceId` is threaded through (not
// reset) so every triangle in the final BVH -- the road's own plus every placement's -- carries a
// unique id; `cache` persists across calls within one Map::load() so a model referenced by several
// placements is only read from disk once.
vector<tox::CollisionTriangle> buildMeshObjectCollisionTriangles(Map* map, tox::Track const& track, filesystem::path const& root, mpp::ResourceManager* resourceMgr,
                                                                 int& nextSurfaceId, std::map<string, shared_ptr<MeshObjectModel>>& cache) {
  vector<tox::CollisionTriangle> triangles;
  for (auto const& placement : track.definition.meshObjects) {
    auto cached = cache.find(placement.modelId);
    if (cached == cache.end()) {
      filesystem::path const modelPath = safeRelativePath(map, root, placement.modelId, "modelId");
      cached = cache.emplace(placement.modelId, loadMeshObjectModel(map, resourceMgr, modelPath)).first;
    }
    MeshObjectModel& model = *cached->second;

    for (size_t meshIndex = 0; meshIndex < model.serializer.getMeshCount(); ++meshIndex) {
      if (!meshNameIsCollidable(model.serializer.getName(meshIndex))) continue;
      if (model.serializer.getPrimitiveType(meshIndex) != mpp::mesh::Primitive::Type::Triangles) continue;

      size_t vertexCount, stride;
      shared_ptr<const int8_t> data;
      model.serializer.getVertexStream(meshIndex, 0, &vertexCount, &stride, &data);
      if (stride != 36) continue;  // not this app's fixed layout -- see cpp/model-tool/docs, skip rather than fail the whole map

      vector<tox::Vec3> positions(vertexCount), normals(vertexCount);
      for (size_t v = 0; v < vertexCount; ++v) {
        auto const bytes = data.get() + v * stride;
        tox::Vec3 localPos(readFloat(bytes, 0), readFloat(bytes, 4), readFloat(bytes, 8));
        tox::Vec3 localNormal(readFloat(bytes, 12), readFloat(bytes, 16), readFloat(bytes, 20));
        positions[v] = placementTransformPosition(placement, localPos);
        normals[v] = placementTransformNormal(placement, localNormal);
      }

      vector<uint32_t> indices;
      if (model.indexStreamIds[meshIndex] == kMeshObjectNoIndexStream) {
        indices.resize(vertexCount);
        for (size_t v = 0; v < vertexCount; ++v) indices[v] = static_cast<uint32_t>(v);
      } else {
        int const indexWidth = model.serializer.getIndexWidth(meshIndex);
        size_t const indexCount = static_cast<size_t>(model.serializer.getPrimitiveCount(meshIndex)) * 3;
        shared_ptr<const uint8_t> const indexData = model.serializer.getIndexData(meshIndex);
        indices.resize(indexCount);
        uint8_t const* p = indexData.get();
        size_t const bytesPerIndex = static_cast<size_t>(indexWidth) / 8;
        for (size_t k = 0; k < indexCount; ++k) {
          uint32_t index = 0;
          memcpy(&index, p, bytesPerIndex);
          indices[k] = index;
          p += bytesPerIndex;
        }
      }

      for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        tox::CollisionTriangle triangle;
        triangle.surfaceId = nextSurfaceId;
        for (int corner = 0; corner < 3; ++corner) {
          uint32_t const index = indices[t + static_cast<size_t>(corner)];
          triangle.positions[corner] = positions[index];
          triangle.normals[corner] = tox::normalizeSafe(normals[index]);
        }
        triangles.push_back(triangle);
      }
      ++nextSurfaceId;
    }
  }
  return triangles;
}

}  // namespace

Map::Map(string const& name, string const& namesp, string const& source,
         map<string, string> const& tags, application::resourcesystem::ResourceLocation* location,
         wp::Logger* logger)
    : applib::Map(name, namesp, source, tags, location, 512), mwLogger(logger) {
}

Map::~Map() = default;

bool Map::load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(renderSystem);
  auto directoryLocation = dynamic_cast<application::resourcesystem::DirectoryResourceLocation*>(mwLocation);
  if (directoryLocation == nullptr)
    throw application::resourcesystem::ResourceException(this, "Track resources require a directory-based resource location.");

  filesystem::path root(directoryLocation->getRootPath());
  filesystem::path modelPath = safeRelativePath(this, root, mModelFileName, "ModelFile");
  filesystem::path dataPath = safeRelativePath(this, root, mTrackDataFileName, "TrackData");

  tox::TrackLoadResult loaded = tox::Track::fromFile(dataPath);
  if (!loaded)
    throw application::resourcesystem::ResourceException(this, "failed to load TrackData '" + dataPath.string() + "': " + loaded.error);
  for (auto const& warning : loaded.warnings)
    mwLogger->warn("Track '" + getQualifiedName() + "' [" + warning.code + "] " + warning.objectId + ": " + warning.message);
  mTrack = make_shared<tox::Track>(std::move(*loaded.track));

  mpp::ModelSerializer serializer(resourceMgr);
  try {
    serializer.load(modelPath.string());
  } catch (exception const& error) {
    throw application::resourcesystem::ResourceException(this, "failed to load ModelFile '" + modelPath.string() + "': " + error.what());
  }

  auto collisionTriangles = buildCollisionTriangles(this, serializer, *mTrack, mTrackMeshNames);
  if (collisionTriangles.empty())
    throw application::resourcesystem::ResourceException(
        this, "TrackMeshes produced no collision triangles -- this track has no drivable/collidable geometry "
              "and cannot be loaded; re-export it from the editor after adding a path or mesh region.");

  // Drivable mesh object placements (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3.3): merged into the
  // same BVH as the road's own collision triangles above, each collidable sub-mesh transformed by
  // its placement. `modelCache` is scoped to this one load() call -- nothing outside it needs a
  // loaded placement model, so there's no reason to keep it (or the mpp::ModelSerializer instances
  // it owns) alive any longer than building this BVH takes.
  int nextSurfaceId = static_cast<int>(mTrackMeshNames.size());
  map<string, shared_ptr<MeshObjectModel>> modelCache;
  auto meshObjectTriangles = buildMeshObjectCollisionTriangles(this, *mTrack, root, resourceMgr, nextSurfaceId, modelCache);
  collisionTriangles.insert(collisionTriangles.end(), make_move_iterator(meshObjectTriangles.begin()), make_move_iterator(meshObjectTriangles.end()));

  mTrack->collisionSurface = make_shared<tox::TrackCollisionSurface>(std::move(collisionTriangles));

  tox::Simulation simulation(*mTrack);
  mStartGridPoses = tox::StartGrid::startingGridPoses(simulation, *mTrack, tox::StartGrid::DEFAULT_SHIP_COUNT);
  if (mStartGridPoses.size() != tox::StartGrid::DEFAULT_SHIP_COUNT)
    throw application::resourcesystem::ResourceException(this, "could not generate all eight starting-grid poses from TrackData.");
  for (tox::Pose& pose : mStartGridPoses) {
    auto contact = mTrack->collisionSurface->nearestAlongAxis(pose.pos, pose.up, 4.0);
    if (!contact)
      throw application::resourcesystem::ResourceException(this, "a starting-grid pose could not be settled onto TrackMeshes.");
    pose.pos = contact->position;
    pose.up = contact->normal;
    pose.forward += pose.up * -glm::dot(pose.forward, pose.up);
    if (glm::dot(pose.forward, pose.forward) < 1e-9)
      throw application::resourcesystem::ResourceException(this, "a starting-grid pose has a degenerate forward direction.");
    pose.forward = tox::normalizeSafe(pose.forward);
  }

  auto meshSpec = trackMeshSpecification();
  auto modelStream = new mpp::ProgrammaticModelStream(resourceMgr);
  for (size_t i = 0; i < serializer.getMeshCount(); ++i) {
    string materialMppName;
    try {
      materialMppName = resolveMaterialMppName(this, serializer.getMaterial(i));
    } catch (exception const& error) {
      if (find(mTrackMeshNames.begin(), mTrackMeshNames.end(), serializer.getName(i)) != mTrackMeshNames.end())
        throw application::resourcesystem::ResourceException(
            this, "listed track mesh '" + serializer.getName(i) + "' has unresolved material: " + error.what());
      mwLogger->warn("Map '" + getQualifiedName() + "': skipping mesh '" + serializer.getName(i) + "': " + error.what());
      continue;
    }

    auto meshId = modelStream->createMesh(serializer.getName(i), meshSpec, materialMppName, 16);
    size_t vertexCount, vertexStride;
    shared_ptr<const int8_t> vertexData;
    serializer.getVertexStream(i, 0, &vertexCount, &vertexStride, &vertexData);
    if (vertexStride != meshSpec.getVertexStrideInBytes())
      throw application::resourcesystem::ResourceException(this, "mesh '" + serializer.getName(i) + "' has an unsupported vertex stride.");
    vector<int8_t> bytes(vertexData.get(), vertexData.get() + vertexCount * vertexStride);
    modelStream->addVertexData(meshId, bytes);
  }

  mMppResource = resourceMgr->declareResource(getQualifiedName(), mpp::ResourceStreamPtr(modelStream)).first;
  mMppResource->acquire(this);
  // Do not call mMppResource->load() here. Map resources can be created on the
  // threaded Willpower loading worker, which has no OpenGL context. The map
  // load state's main-thread MPP phase loads every referenced render resource.
  return true;
}

bool Map::unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(renderSystem);
  WP_UNUSED(resourceMgr);
  if (mMppResource) mMppResource->release(this);
  mTrack.reset();
  mStartGridPoses.clear();
  return true;
}
