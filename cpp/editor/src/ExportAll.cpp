#include "ExportAll.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <numbers>

#include "modelio/MeshLayout.hpp"
#include "modelio/MppModelIo.hpp"
#include "modelio/PbrMaterialRead.hpp"
#include "modelio/Tangents.hpp"

namespace editor {
namespace {

std::uint8_t normalizedByte(double c) { return static_cast<std::uint8_t>(std::clamp(std::lround(c * 255.0), 0L, 255L)); }

// Track meshes with real, always-emitted triangle data (see TrackBake.cpp) -- MeshSurface/
// MeshRail/ReservationWall are declared in tox::GeometryKind but never emitted (dead, from the
// removed MeshRegion system); ZoneSurface/TriggerSurface are the excluded triggers'/zones' visual
// halo, not "track meshes".
bool isExportableTrackMeshKind(tox::GeometryKind kind) {
  return kind == tox::GeometryKind::PathSurface || kind == tox::GeometryKind::PathShell || kind == tox::GeometryKind::PathRail;
}

// Peeks the on-disk IndexData directory entry's own stream count -- the same technique
// MppModelImport.cpp's mppModelIsIndexed uses, duplicated locally rather than shared: neither
// mpp::ModelSerializer nor modelio::readMppModel can answer "is this file indexed" on their own
// (a non-indexed model_io-written file still leaves every mesh's internal index-stream id at 0,
// not a sentinel -- modelio/MppModelIo.hpp's header comment), and mpp::ModelSerializer::
// getIndexData()/getIndexWidth() are unchecked array indexing, unsafe to call speculatively.
bool mppModelIsIndexed(const std::filesystem::path& path) {
  std::ifstream fp(path, std::ios::binary);
  if (!fp) throw std::runtime_error("Could not open '" + path.string() + "'.");
  char magic[4] = {};
  fp.read(magic, 4);
  if (magic[0] != 'M' || magic[1] != 'P' || magic[2] != 'P' || magic[3] != 'M')
    throw std::runtime_error("'" + path.string() + "' is not a valid .mppmodel file (bad magic).");
  fp.seekg(8, std::ios::cur);  // skip u16 versionMajor + u16 versionMinor + u32 flags

  for (int entryIndex = 0; entryIndex < 5; ++entryIndex) {
    std::uint32_t type = 0, start = 0, end = 0, count = 0;
    fp.read(reinterpret_cast<char*>(&type), 4);
    fp.read(reinterpret_cast<char*>(&start), 4);
    fp.read(reinterpret_cast<char*>(&end), 4);
    fp.read(reinterpret_cast<char*>(&count), 4);
    if (!fp) throw std::runtime_error("'" + path.string() + "' is not a valid .mppmodel file (truncated directory).");
    if (type == 4) return count > 0;  // Directory::Entry::Type::IndexData
  }
  throw std::runtime_error("'" + path.string() + "' is not a valid .mppmodel file (missing IndexData directory entry).");
}

float readPackedF32(const std::int8_t* cursor) {
  float value = 0.0f;
  std::memcpy(&value, cursor, sizeof(value));
  return value;
}

// Decodes one vertex from either the 36-byte legacy or 52-byte PBR layout -- position3/normal3/
// texcoord2/colour4 sit at the same fixed offsets (0/12/24/32) in both (see
// modelio::LegacyPbrVertexStride/PbrVertexStride), tangent4 only present at offset 36 in the wider
// one.
modelio::Vertex unpackVertex(const std::int8_t* cursor, std::size_t stride) {
  modelio::Vertex vertex;
  vertex.position[0] = readPackedF32(cursor + 0);
  vertex.position[1] = readPackedF32(cursor + 4);
  vertex.position[2] = readPackedF32(cursor + 8);
  vertex.normal[0] = readPackedF32(cursor + 12);
  vertex.normal[1] = readPackedF32(cursor + 16);
  vertex.normal[2] = readPackedF32(cursor + 20);
  vertex.uv[0] = readPackedF32(cursor + 24);
  vertex.uv[1] = readPackedF32(cursor + 28);
  vertex.colour[0] = static_cast<std::uint8_t>(cursor[32]);
  vertex.colour[1] = static_cast<std::uint8_t>(cursor[33]);
  vertex.colour[2] = static_cast<std::uint8_t>(cursor[34]);
  vertex.colour[3] = static_cast<std::uint8_t>(cursor[35]);
  if (stride >= modelio::PbrVertexStride) {
    vertex.tangent[0] = readPackedF32(cursor + 36);
    vertex.tangent[1] = readPackedF32(cursor + 40);
    vertex.tangent[2] = readPackedF32(cursor + 44);
    vertex.tangent[3] = readPackedF32(cursor + 48);
  }
  return vertex;
}

// Scale, then rotate (yaw about Y, then pitch about X, then roll about Z), then translate --
// ModelPlacementDefinition's documented convention, reimplemented locally rather than shared
// across the editor/host boundary, matching this codebase's own established pattern (mono::
// placementTransformPosition in tungsten-monoxide, TrackCollisionBuild.cpp's copy, and
// TopDownCanvas.cpp's own copy are three existing instances of the same duplication).
tox::Vec3 placementTransformPosition(const ModelPlacement& placement, const tox::Vec3& local) {
  constexpr double kDegToRad = std::numbers::pi / 180.0;
  tox::Vec3 scaled(local.x * placement.scale.x, local.y * placement.scale.y, local.z * placement.scale.z);
  tox::Vec3 rotated = tox::applyAxisAngle(scaled, tox::Vec3(0.0, 1.0, 0.0), placement.rotation.x * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(1.0, 0.0, 0.0), placement.rotation.y * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(0.0, 0.0, 1.0), placement.rotation.z * kDegToRad);
  return rotated + placement.position;
}

// Inverse-transpose of scale-then-rotate, for directions (normals/tangents): divide by scale
// *before* rotating (the opposite order from position), then renormalise to undo scale's effect
// on magnitude. Mirrors mono::placementTransformNormal's exact convention.
tox::Vec3 placementTransformDirection(const ModelPlacement& placement, const tox::Vec3& local) {
  constexpr double kDegToRad = std::numbers::pi / 180.0;
  tox::Vec3 unscaled(placement.scale.x != 0.0 ? local.x / placement.scale.x : local.x,
                     placement.scale.y != 0.0 ? local.y / placement.scale.y : local.y,
                     placement.scale.z != 0.0 ? local.z / placement.scale.z : local.z);
  tox::Vec3 rotated = tox::applyAxisAngle(unscaled, tox::Vec3(0.0, 1.0, 0.0), placement.rotation.x * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(1.0, 0.0, 0.0), placement.rotation.y * kDegToRad);
  rotated = tox::applyAxisAngle(rotated, tox::Vec3(0.0, 0.0, 1.0), placement.rotation.z * kDegToRad);
  return tox::normalizeSafe(rotated);
}

int materialIndexFor(modelio::ModelData& model, std::map<std::string, int>& materialIndexByName, const std::string& name) {
  const auto [it, inserted] = materialIndexByName.try_emplace(name, static_cast<int>(model.materials.size()));
  if (inserted) {
    modelio::MaterialData material;
    material.name = name;
    model.materials.push_back(std::move(material));
  }
  return it->second;
}

void appendTrackMesh(const tox::GeometryBatch& batch, const MaterialCatalog& materialCatalog, modelio::ModelData& model,
                     std::map<std::string, int>& materialIndexByName) {
  modelio::MeshData mesh;
  mesh.name = batch.id;
  mesh.vertices.reserve(batch.vertices.size());
  for (const tox::RenderVertex& v : batch.vertices) {
    modelio::Vertex vertex;
    vertex.position[0] = static_cast<float>(v.position.x);
    vertex.position[1] = static_cast<float>(v.position.y);
    vertex.position[2] = static_cast<float>(v.position.z);
    vertex.normal[0] = static_cast<float>(v.normal.x);
    vertex.normal[1] = static_cast<float>(v.normal.y);
    vertex.normal[2] = static_cast<float>(v.normal.z);
    vertex.uv[0] = static_cast<float>(v.uv.x);
    vertex.uv[1] = static_cast<float>(v.uv.y);
    vertex.colour[0] = normalizedByte(v.rgba.r);
    vertex.colour[1] = normalizedByte(v.rgba.g);
    vertex.colour[2] = normalizedByte(v.rgba.b);
    vertex.colour[3] = normalizedByte(v.rgba.a);
    mesh.vertices.push_back(vertex);
  }
  mesh.indices = batch.indices;

  // MaterialCatalog is keyed by the editor-facing qualified name (what batch.materialKey holds for
  // a path-surface batch); a fixed rail/shell key with no catalog entry passes through unchanged,
  // matching MppModelExport.cpp's own resolveMaterialKey.
  std::string materialName = batch.materialKey;
  std::string texturePath;
  for (const MaterialEntry& entry : materialCatalog.materials()) {
    if (entry.qualifiedName != batch.materialKey) continue;
    materialName = entry.materialQualifiedName;
    if (!entry.texturePaths.empty()) texturePath = entry.texturePaths.front();
    break;
  }

  const int materialIndex = materialIndexFor(model, materialIndexByName, materialName);
  if (!texturePath.empty() && model.materials[static_cast<std::size_t>(materialIndex)].textures.empty())
    model.materials[static_cast<std::size_t>(materialIndex)].textures.push_back({"PBR_BASE_COLOUR_MAP", texturePath});
  mesh.materialIndex = materialIndex;

  modelio::generateTangents(mesh);
  model.meshes.push_back(std::move(mesh));
}

void appendPlacement(const ModelPlacement& placement, const modelxml::ModelXmlDefinition& embeddedModel,
                     const std::filesystem::path& modelBaseDir, modelio::ModelData& model,
                     std::map<std::string, int>& materialIndexByName, modelio::Report& report) {
  const std::filesystem::path mppPath = (modelBaseDir / embeddedModel.modelFile).lexically_normal();

  modelio::ReadModel raw;
  bool indexed = false;
  try {
    indexed = mppModelIsIndexed(mppPath);
    raw = modelio::readMppModel(mppPath, indexed);
  } catch (const std::exception& error) {
    report.warn("export.placement-unreadable", std::string("could not read '") + mppPath.string() + "': " + error.what(), {},
                placement.id);
    return;
  }

  // Resolve every embedded material once per placement, keyed by the raw material name each mesh
  // carries. A name with no matching embedded material is a by-name binding this editor cannot
  // resolve to real PBR values (see this file's header comment) -- it still gets a material, just
  // name-only.
  std::map<std::string, int> localMaterialIndex;
  auto resolveMaterial = [&](const std::string& rawName) {
    const auto cached = localMaterialIndex.find(rawName);
    if (cached != localMaterialIndex.end()) return cached->second;

    modelio::MaterialData material;
    material.name = rawName;
    const auto nameIt = std::find(raw.materialNames.begin(), raw.materialNames.end(), rawName);
    if (nameIt != raw.materialNames.end()) {
      const std::size_t materialIdx = static_cast<std::size_t>(nameIt - raw.materialNames.begin());
      if (materialIdx < raw.materials.size())
        readEmbeddedPbrMaterial(raw.materials[materialIdx], mppPath.parent_path(), material);
    }

    // materialIndexFor pushes a bare name-only entry the first time a name is seen; replace it
    // with the richer `material` just built. A name already declared by an earlier item (e.g. two
    // placements sharing one model) keeps whatever it already has -- redundant to recompute, not
    // wrong to skip.
    const bool alreadyDeclared = materialIndexByName.count(material.name) != 0;
    const int index = materialIndexFor(model, materialIndexByName, material.name);
    if (!alreadyDeclared) model.materials[static_cast<std::size_t>(index)] = material;
    localMaterialIndex.emplace(rawName, index);
    return index;
  };

  for (const modelio::ReadMesh& sourceMesh : raw.meshes) {
    const modelxml::MeshMetadataXmlDefinition* meta = nullptr;
    for (const auto& candidate : embeddedModel.meshes)
      if (candidate.name == sourceMesh.name) {
        meta = &candidate;
        break;
      }
    if (meta != nullptr && !meta->visible) continue;  // game-hidden, matches Map.cpp's own filter

    if (sourceMesh.vertexStride != modelio::LegacyPbrVertexStride && sourceMesh.vertexStride != modelio::PbrVertexStride) {
      report.warn("export.unsupported-vertex-stride",
                  "sub-mesh '" + sourceMesh.name + "' uses an unsupported vertex layout and was skipped", sourceMesh.name,
                  placement.id);
      continue;
    }

    modelio::MeshData mesh;
    mesh.name = "meshobject-" + placement.id + "-" + sourceMesh.name;
    mesh.vertices.reserve(sourceMesh.vertexCount);
    for (std::size_t v = 0; v < sourceMesh.vertexCount; ++v) {
      modelio::Vertex vertex = unpackVertex(sourceMesh.vertexBytes.data() + v * sourceMesh.vertexStride, sourceMesh.vertexStride);

      const tox::Vec3 worldPos = placementTransformPosition(
          placement, tox::Vec3(vertex.position[0], vertex.position[1], vertex.position[2]));
      const tox::Vec3 worldNormal = placementTransformDirection(
          placement, tox::Vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]));
      vertex.position[0] = static_cast<float>(worldPos.x);
      vertex.position[1] = static_cast<float>(worldPos.y);
      vertex.position[2] = static_cast<float>(worldPos.z);
      vertex.normal[0] = static_cast<float>(worldNormal.x);
      vertex.normal[1] = static_cast<float>(worldNormal.y);
      vertex.normal[2] = static_cast<float>(worldNormal.z);
      if (sourceMesh.vertexStride >= modelio::PbrVertexStride) {
        const tox::Vec3 worldTangent =
            placementTransformDirection(placement, tox::Vec3(vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]));
        vertex.tangent[0] = static_cast<float>(worldTangent.x);
        vertex.tangent[1] = static_cast<float>(worldTangent.y);
        vertex.tangent[2] = static_cast<float>(worldTangent.z);
      }
      mesh.vertices.push_back(vertex);
    }

    if (indexed) {
      mesh.indices = sourceMesh.indices;
    } else {
      mesh.indices.resize(sourceMesh.vertexCount);
      for (std::size_t i = 0; i < mesh.indices.size(); ++i) mesh.indices[i] = static_cast<std::uint32_t>(i);
    }
    if (sourceMesh.vertexStride < modelio::PbrVertexStride) modelio::generateTangents(mesh);

    mesh.materialIndex = resolveMaterial(sourceMesh.material);
    model.meshes.push_back(std::move(mesh));
  }
}

}  // namespace

std::vector<ExportableItem> collectExportableItems(const TrackDefinition& track, const tox::Track& baked) {
  std::vector<ExportableItem> items;
  for (std::size_t i = 0; i < baked.geometry.size(); ++i) {
    const tox::GeometryBatch& batch = baked.geometry[i];
    if (!isExportableTrackMeshKind(batch.kind)) continue;
    items.push_back({ExportableItem::Kind::TrackMesh, batch.id, i});
  }
  for (std::size_t i = 0; i < track.meshObjects.size(); ++i) {
    const ModelPlacement& placement = track.meshObjects[i];
    std::string label = placement.id.empty() ? placement.modelId : placement.id;
    items.push_back({ExportableItem::Kind::Placement, label, i});
  }
  return items;
}

modelio::ModelData buildExportModelData(const EditorState& state, const tox::Track& baked,
                                        const std::vector<ExportableItem>& items, const std::vector<bool>& checked,
                                        const MaterialCatalog& materialCatalog, const std::filesystem::path& modelBaseDir,
                                        modelio::Report& report) {
  modelio::ModelData model;
  std::map<std::string, int> materialIndexByName;

  for (std::size_t i = 0; i < items.size() && i < checked.size(); ++i) {
    if (!checked[i]) continue;
    const ExportableItem& item = items[i];
    if (item.kind == ExportableItem::Kind::TrackMesh) {
      appendTrackMesh(baked.geometry[item.index], materialCatalog, model, materialIndexByName);
    } else {
      const ModelPlacement& placement = state.track().meshObjects[item.index];
      const modelxml::ModelXmlDefinition* embeddedModel = state.findModel(placement.modelId);
      if (embeddedModel == nullptr) {
        report.warn("export.placement-model-missing", "placement references an unknown model and was skipped", {}, placement.id);
        continue;
      }
      appendPlacement(placement, *embeddedModel, modelBaseDir, model, materialIndexByName, report);
    }
  }

  if (model.materials.empty()) model.materials.push_back(modelio::MaterialData{});
  return model;
}

}  // namespace editor
