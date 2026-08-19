#include "AssImpImport.hpp"

#include <utility>

#include "NormalSmoothing.hpp"
#include "ObjSmoothingGroups.hpp"
#include "modelio/AssetImport.hpp"
#include "modelio/Diagnostics.hpp"
#include "modelio/ModelData.hpp"

namespace modeltool {
namespace {

// modelio::MaterialData carries the full PBR surface; model-tool only ever renders through mpp's
// core default 3D program with a single diffuse sampler (ADR 0001 D4/D6), so base colour is the
// one binding worth keeping. The rest is deliberately discarded rather than half-displayed.
constexpr char kBaseColourSampler[] = "PBR_BASE_COLOUR_MAP";

ImportedMaterial convertMaterial(const modelio::MaterialData& source) {
  ImportedMaterial out;
  out.name = source.name;
  out.skippedEmbeddedTexture = source.skippedEmbeddedTexture;
  out.origin = MaterialOrigin::Embedded;
  for (const modelio::TextureRef& texture : source.textures) {
    if (texture.sampler != kBaseColourSampler) continue;
    out.diffuseTexturePath = texture.path;
    break;
  }
  return out;
}

ImportedMesh convertMesh(const modelio::MeshData& source) {
  ImportedMesh out;
  out.name = source.name;
  out.materialIndex = source.materialIndex;
  out.indices = source.indices;

  out.vertices.resize(source.vertices.size());
  for (std::size_t i = 0; i < source.vertices.size(); ++i) {
    const modelio::Vertex& in = source.vertices[i];
    ImportedVertex& v = out.vertices[i];
    v.px = in.position[0];
    v.py = in.position[1];
    v.pz = in.position[2];
    v.nx = in.normal[0];
    v.ny = in.normal[1];
    v.nz = in.normal[2];
    v.u = in.uv[0];
    v.v = in.uv[1];
    v.r = in.colour[0];
    v.g = in.colour[1];
    v.b = in.colour[2];
    v.a = in.colour[3];
    // Tangents are dropped: model-tool's fixed layout has no tangent4 channel.
  }
  return out;
}

}  // namespace

std::optional<ImportedModel> importModel(const std::string& utf8Path, std::string* outError) {
  modelio::ImportOptions options;
  // Real shared-vertex indexing, not a per-face vertex soup (ADR 0001 D5 -- unlike track geometry,
  // an imported mesh's vertices are genuinely shared).
  options.joinIdenticalVertices = true;
  // An image packed in the container costs this app a placeholder texture, not a broken asset: it
  // references materials by name and only wants a path to preview with (ADR 0001 D4).
  options.embeddedTextures = modelio::EmbeddedTexturePolicy::Skip;
  // AssImp commonly synthesizes a placeholder material for meshes that reference none. If nothing
  // ends up using it, don't create, display or declare it at all.
  options.pruneUnreferencedMaterials = true;

  modelio::Report report;
  const std::optional<modelio::ModelData> source = modelio::importAsset(utf8Path, options, report);
  if (!source) {
    if (outError) {
      // The report holds one line per finding; the first error is the one that stopped the import.
      *outError = report.format();
      if (outError->empty()) *outError = "the file could not be read";
    }
    return std::nullopt;
  }

  ImportedModel out;
  out.sourcePath = source->sourcePath;

  out.materials.reserve(source->materials.size());
  for (const modelio::MaterialData& material : source->materials) out.materials.push_back(convertMaterial(material));
  // ModelResources.cpp always resolves a mesh's material by indexing ImportedModel::materials, so
  // there must always be at least one entry. importAsset() guarantees this, but the invariant is
  // load-bearing enough downstream to restate rather than assume.
  if (out.materials.empty()) out.materials.push_back(ImportedMaterial{"default", std::nullopt, false});

  out.meshes.reserve(source->meshes.size());
  for (const modelio::MeshData& mesh : source->meshes) out.meshes.push_back(convertMesh(mesh));

  // Normals are always recomputed from winding order, never trusted from the source file
  // (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 4.2) -- smoothing-group-aware where the format makes
  // that recoverable, which is OBJ only, since AssImp's public aiMesh API exposes smoothing groups
  // for no format at all. See ObjSmoothingGroups.hpp and NormalSmoothing.hpp.
  const auto smoothingGroups = extractObjSmoothingGroups(utf8Path, out);
  recomputeNormals(out, smoothingGroups.has_value() ? &*smoothingGroups : nullptr);

  return out;
}

}  // namespace modeltool
