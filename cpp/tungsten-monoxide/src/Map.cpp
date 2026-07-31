#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
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
