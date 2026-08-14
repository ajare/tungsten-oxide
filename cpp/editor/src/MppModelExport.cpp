#include "MppModelExport.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>

#include "willpower/common/tinyxml2.h"

#include "modelio/MeshLayout.hpp"
#include "modelio/ModelData.hpp"
#include "modelio/MppModelIo.hpp"
#include "modelio/Tangents.hpp"

namespace editor {
namespace {

std::uint8_t normalizedByte(double c) { return static_cast<std::uint8_t>(std::clamp(std::lround(c * 255.0), 0L, 255L)); }

// A materialKey with no entry (the fixed rail/shell/zone/trigger materials, or an empty/legacy
// "road" literal) passes through unchanged -- those already name real Materials directly.
std::string resolveMaterialKey(const std::string& materialKey, const std::map<std::string, std::string>& trackMaterialToMaterial) {
  const auto it = trackMaterialToMaterial.find(materialKey);
  return it == trackMaterialToMaterial.end() ? materialKey : it->second;
}

// One tox::GeometryBatch -> one modelio::MeshData, sharing one materialIndex per distinct resolved
// material key. Every batch is already an unshared triangle soup (TrackBake.cpp's Builder::tri()
// and TrackMesh.cpp's addTriangle() give every triangle three brand-new vertices), so indices are
// the identity permutation -- modelio::generateTangents accumulates per-triangle regardless, and a
// non-indexed target (see exportTrackToMppModel) consumes them back out again unchanged.
modelio::ModelData buildModelData(const tox::Track& track, const std::map<std::string, std::string>& trackMaterialToMaterial) {
  modelio::ModelData model;
  std::map<std::string, int> materialIndexByName;
  for (const tox::GeometryBatch& batch : track.geometry) {
    modelio::MeshData mesh;
    mesh.name = batch.id;
    mesh.vertices.reserve(batch.vertices.size());
    for (const auto& v : batch.vertices) {
      modelio::Vertex vertex;
      vertex.position[0] = static_cast<float>(v.position.x);
      vertex.position[1] = static_cast<float>(v.position.y);
      vertex.position[2] = static_cast<float>(v.position.z);
      vertex.normal[0] = static_cast<float>(v.normal.x);
      vertex.normal[1] = static_cast<float>(v.normal.y);
      vertex.normal[2] = static_cast<float>(v.normal.z);
      // uv is {0,0} whenever !batch.hasUv (TrackBake.cpp/TrackMesh.cpp never set it otherwise),
      // which is exactly modelio::Vertex's own default for an unsupplied channel.
      vertex.uv[0] = static_cast<float>(v.uv.x);
      vertex.uv[1] = static_cast<float>(v.uv.y);
      vertex.colour[0] = normalizedByte(v.rgba.r);
      vertex.colour[1] = normalizedByte(v.rgba.g);
      vertex.colour[2] = normalizedByte(v.rgba.b);
      vertex.colour[3] = normalizedByte(v.rgba.a);
      mesh.vertices.push_back(vertex);
    }
    mesh.indices.resize(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.indices.size(); ++i) mesh.indices[i] = static_cast<std::uint32_t>(i);

    const std::string materialName = resolveMaterialKey(batch.materialKey, trackMaterialToMaterial);
    const auto [it, inserted] = materialIndexByName.try_emplace(materialName, static_cast<int>(model.materials.size()));
    if (inserted) {
      modelio::MaterialData material;
      material.name = materialName;
      model.materials.push_back(material);
    }
    mesh.materialIndex = it->second;

    modelio::generateTangents(mesh);
    model.meshes.push_back(std::move(mesh));
  }
  return model;
}

// Builds the <Models> list (TRACK_MODEL_LIST_PLAN.md): the primary Track-type Model, regenerated
// fresh every save from `bakedTrack`'s own collidable geometry -- exactly the same batch-kind filter
// the old flat <TrackMeshes> list used (Map.cpp's gameplayKind()) -- followed by `otherModels`
// written back verbatim, byte-identical to how they were parsed (Milestone 6's "Load Model" is the
// only thing that ever adds to that list; this function never edits it). Printed via TinyXML2 with
// no attempt at matching the surrounding hand-built string XML's indentation -- upsertTrackResource
// (TrackResourceDocument.cpp) reparses and reprints the whole document before it's ever saved to
// disk, so whatever whitespace lands here is discarded and reformatted anyway.
std::string buildModelsXml(const std::string& primaryModelId, const std::string& mppModelFileName,
                           const std::string& trackDataFileName, const tox::Track& bakedTrack,
                           const std::vector<modelxml::ModelXmlDefinition>& otherModels) {
  tinyxml2::XMLDocument doc;
  tinyxml2::XMLElement* modelsElem = doc.NewElement("Models");
  doc.InsertEndChild(modelsElem);

  modelxml::ModelXmlDefinition primary;
  primary.id = primaryModelId;
  primary.modelFile = mppModelFileName;
  primary.trackData = trackDataFileName;
  // Must stay in lock-step with Map.cpp's gameplayKind() -- same set, same reasoning: everything a
  // ship can physically contact goes into the collision BVH (drivable surfaces, reservation walls,
  // Path- and MeshRegion-authored rails). PathShell stays out -- render-only.
  for (const tox::GeometryBatch& batch : bakedTrack.geometry) {
    if (batch.kind != tox::GeometryKind::PathSurface && batch.kind != tox::GeometryKind::MeshSurface &&
        batch.kind != tox::GeometryKind::ReservationWall && batch.kind != tox::GeometryKind::PathRail &&
        batch.kind != tox::GeometryKind::MeshRail)
      continue;
    primary.meshes.push_back({batch.id, modelxml::MeshType::Track, true});
  }
  tinyxml2::XMLElement* primaryElem = doc.NewElement("Model");
  modelsElem->InsertEndChild(primaryElem);
  modelxml::writeModelFragment(primaryElem, primary);

  for (const modelxml::ModelXmlDefinition& other : otherModels) {
    tinyxml2::XMLElement* otherElem = doc.NewElement("Model");
    modelsElem->InsertEndChild(otherElem);
    modelxml::writeModelFragment(otherElem, other);
  }

  tinyxml2::XMLPrinter printer;
  modelsElem->Accept(&printer);
  return printer.CStr();
}

}  // namespace

MppModelExportResult exportTrackToMppModel(const tox::Track& track, const std::map<std::string, std::string>& trackMaterialToMaterial) {
  const modelio::ModelData model = buildModelData(track, trackMaterialToMaterial);
  // 52 bytes/vertex, non-indexed -- docs/GLTF_IMPORT_PLAN.md M4. Non-indexed because every tox
  // geometry batch is already an unshared triangle soup (see buildModelData's own comment), so an
  // index buffer holding 0,1,2,3,... would be pure redundancy; cpp/tungsten-monoxide's Map.cpp
  // derives primitiveCount as vertexCount / 3 rather than from indices for exactly this reason.
  const mpp::mesh::MeshSpecification meshSpec = modelio::gameMeshSpecification(/*indexed=*/false, /*pbr=*/true);

  // modelio::writeMppModelWithNamedMaterials writes straight to a filesystem path, but
  // TrackResourceSave.cpp's Save/Save As flow builds every output file in memory first
  // (prepareTrackSave) and installs all three together, atomically, only once every one of them
  // has been prepared successfully (commitTrackSave) -- so this writes to a scratch file, reads it
  // back into memory, and discards the scratch file, preserving that contract without threading
  // model_io's own file I/O through the save transaction.
  std::error_code ec;
  const std::filesystem::path scratchPath =
      std::filesystem::temp_directory_path(ec) /
      ("tungsten-oxide-track-export-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mppmodel");
  if (ec) throw std::runtime_error("Could not locate a temporary directory to build the track model in: " + ec.message());

  modelio::Report report;
  const bool ok = modelio::writeMppModelWithNamedMaterials(model, meshSpec, scratchPath, report);
  if (!ok) {
    std::filesystem::remove(scratchPath, ec);
    throw std::runtime_error("Could not build the track's .mppmodel: " + report.format());
  }

  std::ifstream input(scratchPath, std::ios::binary);
  if (!input) {
    std::filesystem::remove(scratchPath, ec);
    throw std::runtime_error("Could not reopen '" + scratchPath.string() + "' to read the exported track model back.");
  }
  MppModelExportResult result;
  result.bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  result.meshCount = model.meshes.size();
  input.close();
  std::filesystem::remove(scratchPath, ec);
  return result;
}

namespace {

// The materials TrackBake.cpp/TrackMesh.cpp always assign to rail/mesh-region/shell/zone/trigger
// geometry, regardless of what any path is assigned -- see MppModelExport.hpp's comment.
constexpr char kDefaultRailMaterial[] = "Tracks/DefaultRailMaterial";
constexpr char kDefaultMeshMaterial[] = "Tracks/DefaultMeshMaterial";
constexpr char kDefaultShellMaterial[] = "Tracks/DefaultShellMaterial";
constexpr char kDefaultZoneMaterial[] = "Tracks/DefaultZoneMaterial";
constexpr char kDefaultTriggerMaterial[] = "Tracks/DefaultTriggerMaterial";

// model-tool's own untextured-white placeholder (MaterialLibrary.cpp's defaultFallbackMaterial()):
// a raw, unnamespaced key model-tool bakes into a mesh's material metadata when the mesh was never
// assigned a real one there. Unlike the fixed materials above, its id (what Map.cpp's
// resolveMaterialMppName() looks it up by, matched verbatim against the .mppmodel's own baked
// string) is NOT the same as its qualified resource name -- it's declared under the Tracks
// namespace in Resources.yaml (alongside DefaultMeshMaterial et al.) but referenced bare, since
// that's the literal string model-tool writes, with no namespace prefix of its own. Included
// unconditionally on every export, regardless of whether this track's own placements are known to
// need it: core never loads a placement's .mppmodel (this editor can't introspect what material
// key it actually embeds), so the only way to guarantee this one well-known fallback name always
// resolves for ANY drivable mesh object placement is to always declare it, the same as the fixed
// rail/mesh/shell/zone/trigger materials above.
constexpr char kModelToolDefaultFallbackMaterialId[] = "ModelTool.DefaultFallbackMaterial3D";
constexpr char kModelToolDefaultFallbackMaterialRef[] = "Tracks/ModelTool.DefaultFallbackMaterial3D";

std::string xmlEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c; break;
    }
  }
  return out;
}

}  // namespace

std::string buildTrackResourceXmlForName(const TrackDefinition& track, const tox::Track& bakedTrack,
                                         const std::string& resourceName, const std::string& mppModelFileName,
                                         const std::string& trackDataFileName,
                                         const std::map<std::string, std::string>& trackMaterialToMaterial,
                                         const std::string& primaryModelId,
                                         const std::vector<modelxml::ModelXmlDefinition>& otherModels) {
  // Every distinct material this track's curves are actually assigned to, in first-seen order,
  // plus the fixed rail/mesh/shell/zone/trigger bindings every export depends on regardless of
  // curve content. Resolved through trackMaterialToMaterial first (see MppModelExport.hpp's
  // comment) so this dependency list always matches what the exported mesh's own material
  // reference resolves to -- choices that map to the same PBR binding key collapse to one
  // dependency here, which `seen` already handles.
  std::vector<std::string> materials;
  std::set<std::string> seen;
  for (const auto& path : track.paths) {
    if (path.material.empty()) continue;
    const std::string resolved = resolveMaterialKey(path.material, trackMaterialToMaterial);
    if (!seen.insert(resolved).second) continue;
    materials.push_back(resolved);
  }
  for (const char* fixed :
       {kDefaultRailMaterial, kDefaultMeshMaterial, kDefaultShellMaterial, kDefaultZoneMaterial, kDefaultTriggerMaterial}) {
    if (seen.insert(fixed).second) materials.push_back(fixed);
  }

  std::string xml = "<?xml version=\"1.0\"?>\n<Resources>\n\t<Namespace name=\"Tracks\">\n";
  // No `location=` attribute: Track is always composite (it lists PBR binding dependents below),
  // and ResourceManager::instantiateResource() unconditionally discards a composite resource's own
  // `location`/source. The .mppmodel filename instead travels via <Definition><File> below, which
  // MapTungstenMonoxideDefinitionFactory::create() reads into Map::mModelFileName.
  xml += "\t\t<Resource type=\"Track\" name=\"" + xmlEscape(resourceName) + "\">\n";

  {
    xml += "\t\t\t<DependentResources>\n";
    // id == ref (the qualified name) -- Map::load() (cpp/tungsten-monoxide/src/Map.cpp) resolves
    // each mesh's material by calling getDependentResource() with the exact "Tracks/..." string
    // baked into the mesh's GeometryBatch.materialKey, so the id must match that verbatim. `seen`
    // above already dedupes by qualified name, so no id collision is possible here.
    for (const std::string& qualifiedName : materials) {
      xml += "\t\t\t\t<DependentResource id=\"" + xmlEscape(qualifiedName) + "\" ref=\"" + xmlEscape(qualifiedName) + "\" />\n";
    }
    // id != ref here -- see kModelToolDefaultFallbackMaterialId's own comment above.
    xml += "\t\t\t\t<DependentResource id=\"" + xmlEscape(kModelToolDefaultFallbackMaterialId) + "\" ref=\"" +
           xmlEscape(kModelToolDefaultFallbackMaterialRef) + "\" />\n";
    xml += "\t\t\t</DependentResources>\n";
  }

  // <File> carries the .mppmodel filename (see the no-`location=` comment above). The
  // <Definitions> block itself is also mandatory even apart from that: a resource with none at all
  // still gets an implicit factory="" one synthesized by ResourceLocation::scanResourceElement(),
  // and no (Map, "") definition factory is registered -- only (Map, "Track") is (see
  // cpp/tungsten-monoxide/src/DLL.cpp). Omitting it throws "could not find a definition factory".
  xml += "\t\t\t<Definitions>\n\t\t\t\t<Definition factory=\"Track\">\n";
  xml += buildModelsXml(primaryModelId, mppModelFileName, trackDataFileName, bakedTrack, otherModels);
  xml += "\n\t\t\t\t</Definition>\n\t\t\t</Definitions>\n";

  xml += "\t\t</Resource>\n\t</Namespace>\n</Resources>\n";
  return xml;
}

}  // namespace editor
