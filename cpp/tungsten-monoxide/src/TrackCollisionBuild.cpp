#include "TrackCollisionBuild.h"

#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>

using namespace std;

namespace mono {

namespace {

bool gameplayKind(tox::GeometryKind kind) {
  // Mirrors Map.cpp's own gameplayKind() exactly -- see that file's comment on why PathShell stays
  // out and why this must stay in lock-step with the editor's <TrackMeshes> export loop.
  return kind == tox::GeometryKind::PathSurface || kind == tox::GeometryKind::MeshSurface ||
         kind == tox::GeometryKind::ReservationWall || kind == tox::GeometryKind::PathRail ||
         kind == tox::GeometryKind::MeshRail;
}

bool matchesExportedFloat(double expected, double actual) {
  return static_cast<double>(static_cast<float>(expected)) == actual;
}

uint32_t readRawU32(ifstream& fp, string const& utf8Path) {
  uint32_t value = 0;
  fp.read(reinterpret_cast<char*>(&value), sizeof(value));
  if (!fp) throw runtime_error("unexpected end of file while reading mesh metadata from '" + utf8Path + "'.");
  return value;
}

void skipRawString(ifstream& fp, string const& utf8Path) {
  uint32_t const len = readRawU32(fp, utf8Path);
  fp.seekg(static_cast<streamoff>(len), ios::cur);
  if (!fp) throw runtime_error("unexpected end of file while reading mesh metadata from '" + utf8Path + "'.");
}

vector<uint32_t> readMeshObjectIndexStreamIds(string const& utf8Path, size_t meshCount) {
  ifstream fp(utf8Path, ios::binary);
  if (!fp) throw runtime_error("could not reopen '" + utf8Path + "' to inspect mesh metadata.");

  fp.seekg(12, ios::beg);  // Header: 4-byte magic + u16 + u16 + u32.
  uint32_t meshMetadataStart = 0;
  for (int entryIndex = 0; entryIndex < 6; ++entryIndex) {
    uint32_t const type = readRawU32(fp, utf8Path);
    uint32_t const startOffset = readRawU32(fp, utf8Path);
    readRawU32(fp, utf8Path);  // endOffset, unused
    readRawU32(fp, utf8Path);  // count, unused
    if (type == 5) meshMetadataStart = startOffset;  // Directory::Entry::Type::MeshMetadata
  }

  fp.seekg(meshMetadataStart, ios::beg);
  vector<uint32_t> indexStreamIds;
  indexStreamIds.reserve(meshCount);
  for (size_t i = 0; i < meshCount; ++i) {
    skipRawString(fp, utf8Path);  // name
    readRawU32(fp, utf8Path);      // primitiveType
    readRawU32(fp, utf8Path);      // primitiveCount
    skipRawString(fp, utf8Path);  // material
    uint32_t const numVertexBuffers = readRawU32(fp, utf8Path);
    for (uint32_t v = 0; v < numVertexBuffers; ++v) readRawU32(fp, utf8Path);  // vertex buffer ids
    indexStreamIds.push_back(readRawU32(fp, utf8Path));                       // indexStream
  }
  return indexStreamIds;
}

}  // namespace

float readFloat(int8_t const* bytes, size_t offset) {
  float value;
  memcpy(&value, bytes + offset, sizeof(value));
  return value;
}

// Local-to-world position: scale, then rotate (yaw about Y, then pitch about X, then roll about Z
// -- ModelPlacementDefinition's own documented convention), then translate.
tox::Vec3 placementTransformPosition(tox::ModelPlacementDefinition const& placement, tox::Vec3 const& local) {
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
tox::Vec3 placementTransformNormal(tox::ModelPlacementDefinition const& placement, tox::Vec3 const& localNormal) {
  tox::Vec3 unscaled(placement.scale.x != 0.0 ? localNormal.x / placement.scale.x : localNormal.x,
                     placement.scale.y != 0.0 ? localNormal.y / placement.scale.y : localNormal.y,
                     placement.scale.z != 0.0 ? localNormal.z / placement.scale.z : localNormal.z);
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  tox::Vec3 rotated = tox::applyAxisAngle(unscaled, tox::Vec3(0.0, 1.0, 0.0), placement.rotation.x * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(1.0, 0.0, 0.0), placement.rotation.y * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(0.0, 0.0, 1.0), placement.rotation.z * kDegToRad);
  return tox::normalizeSafe(rotated);
}

string resolveModelFileReference(string const& modelId, vector<EmbeddedModelRef> const& embeddedModels) {
  for (auto const& model : embeddedModels)
    if (model.id == modelId) return model.modelFileReference;
  return modelId;
}

ModelMeshMeta const* findMeshMeta(string const& modelId, string const& meshName, vector<EmbeddedModelRef> const& embeddedModels) {
  for (auto const& model : embeddedModels) {
    if (model.id != modelId) continue;
    for (auto const& mesh : model.meshes)
      if (mesh.name == meshName) return &mesh;
    return nullptr;
  }
  return nullptr;
}

filesystem::path safeRelativePath(filesystem::path const& root, string const& value, char const* field) {
  filesystem::path relative(value);
  if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
    throw runtime_error(string(field) + " must be a non-empty relative path.");
  for (auto const& part : relative)
    if (part == "..") throw runtime_error(string(field) + " may not traverse outside the resource directory.");
  return (root / relative).lexically_normal();
}

vector<string> collidableGeometryBatchIds(tox::Track const& track) {
  vector<string> ids;
  for (auto const& batch : track.geometry)
    if (gameplayKind(batch.kind)) ids.push_back(batch.id);
  return ids;
}

vector<tox::CollisionTriangle> buildCollisionTriangles(
    mpp::ModelSerializer& serializer, tox::Track const& track, vector<string> const& selectedNames) {
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
    throw runtime_error(detail + ".");
  }

  std::map<string, size_t> modelByName;
  for (size_t i = 0; i < serializer.getMeshCount(); ++i) {
    string const& name = serializer.getName(i);
    if (!modelByName.emplace(name, i).second) throw runtime_error("model contains duplicate mesh name '" + name + "'.");
  }

  vector<tox::CollisionTriangle> triangles;
  int surfaceId = 0;
  for (string const& name : selectedNames) {
    auto modelIt = modelByName.find(name);
    if (modelIt == modelByName.end()) throw runtime_error("listed track mesh '" + name + "' is missing from ModelFile.");
    size_t meshIndex = modelIt->second;
    if (serializer.getPrimitiveType(meshIndex) != mpp::mesh::Primitive::Type::Triangles)
      throw runtime_error("listed track mesh '" + name + "' is not triangular.");

    size_t vertexCount, stride;
    shared_ptr<const int8_t> data;
    serializer.getVertexStream(meshIndex, 0, &vertexCount, &stride, &data);
    if (stride != 36 || vertexCount % 3 != 0)
      throw runtime_error("listed track mesh '" + name + "' must use the exported 36-byte non-indexed triangle layout.");

    auto const& expected = *expectedByName.at(name);
    if (expected.vertices.size() != vertexCount)
      throw runtime_error("listed track mesh '" + name + "' triangle count does not match TrackData.");

    vector<tox::RenderVertex> decoded;
    decoded.reserve(vertexCount);
    for (size_t v = 0; v < vertexCount; ++v) {
      auto bytes = data.get() + v * stride;
      tox::RenderVertex vertex;
      vertex.position = {readFloat(bytes, 0), readFloat(bytes, 4), readFloat(bytes, 8)};
      vertex.normal = {readFloat(bytes, 12), readFloat(bytes, 16), readFloat(bytes, 20)};
      if (glm::dot(vertex.normal, vertex.normal) < 1e-12)
        throw runtime_error("listed track mesh '" + name + "' contains an unusable vertex normal.");
      auto const& reference = expected.vertices[v];
      if (!matchesExportedFloat(reference.position.x, vertex.position.x) ||
          !matchesExportedFloat(reference.position.y, vertex.position.y) ||
          !matchesExportedFloat(reference.position.z, vertex.position.z) ||
          !matchesExportedFloat(reference.normal.x, vertex.normal.x) ||
          !matchesExportedFloat(reference.normal.y, vertex.normal.y) ||
          !matchesExportedFloat(reference.normal.z, vertex.normal.z))
        throw runtime_error("listed track mesh '" + name + "' vertex data does not match TrackData export geometry.");
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

shared_ptr<MeshObjectModel> loadMeshObjectModel(mpp::ResourceManager* resourceMgr, filesystem::path const& path) {
  auto model = make_shared<MeshObjectModel>();
  model->serializer = mpp::ModelSerializer(resourceMgr);
  try {
    model->serializer.load(path.string());
  } catch (exception const& error) {
    throw runtime_error("failed to load drivable mesh object model '" + path.string() + "': " + error.what());
  }
  model->indexStreamIds = readMeshObjectIndexStreamIds(path.string(), model->serializer.getMeshCount());
  return model;
}

vector<tox::CollisionTriangle> buildMeshObjectCollisionTriangles(
    tox::Track const& track, filesystem::path const& root, mpp::ResourceManager* resourceMgr, int& nextSurfaceId,
    std::map<string, shared_ptr<MeshObjectModel>>& cache, vector<EmbeddedModelRef> const& embeddedModels) {
  vector<tox::CollisionTriangle> triangles;
  for (auto const& placement : track.definition.meshObjects) {
    auto cached = cache.find(placement.modelId);
    if (cached == cache.end()) {
      filesystem::path const modelPath = safeRelativePath(root, resolveModelFileReference(placement.modelId, embeddedModels), "modelId");
      cached = cache.emplace(placement.modelId, loadMeshObjectModel(resourceMgr, modelPath)).first;
    }
    MeshObjectModel& model = *cached->second;

    for (size_t meshIndex = 0; meshIndex < model.serializer.getMeshCount(); ++meshIndex) {
      ModelMeshMeta const* meta = findMeshMeta(placement.modelId, model.serializer.getName(meshIndex), embeddedModels);
      // Default Physical (collidable) when no metadata is known -- matches CollidableFlag.hpp's old
      // "every mesh collidable" default this replaces.
      if (meta != nullptr && meta->type != ModelMeshType::Physical) continue;
      if (model.serializer.getPrimitiveType(meshIndex) != mpp::mesh::Primitive::Type::Triangles) continue;

      size_t vertexCount, stride;
      shared_ptr<const int8_t> data;
      model.serializer.getVertexStream(meshIndex, 0, &vertexCount, &stride, &data);
      if (stride != 36) continue;  // not this app's fixed layout -- skip rather than fail the whole map

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

}  // namespace mono
