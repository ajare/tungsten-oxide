#include "MppSave.hpp"

#include <exception>

#include "ModelResources.hpp"
#include "modelio/Diagnostics.hpp"
#include "modelio/MeshLayout.hpp"
#include "modelio/ModelData.hpp"
#include "modelio/MppModelIo.hpp"

namespace modeltool {
namespace {

// ImportedModel -> the renderer-neutral type modelio writes from. Only the channels model-tool's
// fixed 36-byte layout actually names are carried; tangents stay at their defaults and are never
// written, because gameMeshSpecification(indexed, /*pbr=*/false) declares no tangent4.
modelio::ModelData toModelData(const BuiltModel& built, MaterialLibrary& materialLibrary) {
  modelio::ModelData out;
  out.sourcePath = built.source.sourcePath;

  out.materials.reserve(built.source.materials.size());
  for (const ImportedMaterial& material : built.source.materials) {
    modelio::MaterialData converted;
    // A DefaultFallback mesh resolves to this app's own shared default-white material, which is not
    // a model-owned entry and so is named directly rather than through built.source.
    converted.name = material.origin == MaterialOrigin::DefaultFallback
                         ? materialLibrary.defaultFallbackMaterial()->getName()
                         : material.name;
    out.materials.push_back(std::move(converted));
  }

  out.meshes.reserve(built.source.meshes.size());
  for (const ImportedMesh& mesh : built.source.meshes) {
    modelio::MeshData converted;
    // Per-mesh Type/Visible metadata no longer rides along in the exported name
    // (TRACK_MODEL_LIST_PLAN.md Milestone 3.2 retired that convention) -- it lives only in the
    // associated <Model> XML, so the mesh name is written completely unchanged.
    converted.name = mesh.name;
    converted.materialIndex = mesh.materialIndex;
    converted.indices = mesh.indices;
    converted.vertices.resize(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
      const ImportedVertex& v = mesh.vertices[i];
      modelio::Vertex& target = converted.vertices[i];
      target.position[0] = v.px;
      target.position[1] = v.py;
      target.position[2] = v.pz;
      target.normal[0] = v.nx;
      target.normal[1] = v.ny;
      target.normal[2] = v.nz;
      target.uv[0] = v.u;
      target.uv[1] = v.v;
      target.colour[0] = v.r;
      target.colour[1] = v.g;
      target.colour[2] = v.b;
      target.colour[3] = v.a;
    }
    out.meshes.push_back(std::move(converted));
  }

  return out;
}

}  // namespace

bool saveModelAsMppModel(const BuiltModel& built, MaterialLibrary& materialLibrary, const std::string& utf8Path, std::string* outError) {
  try {
    // Every non-fallback name is checked against materialLibrary here, so saving fails loudly
    // rather than silently producing a file whose mesh.material fields reference something that is
    // no longer loaded (and so the companion XML, built from this same ImportedModel by main.cpp,
    // is guaranteed to describe something real).
    for (const ImportedMaterial& material : built.source.materials) {
      if (material.origin == MaterialOrigin::DefaultFallback) continue;
      if (!materialLibrary.materials().count(material.name)) {
        if (outError) *outError = "Material '" + material.name + "' is no longer loaded.";
        return false;
      }
    }

    modelio::Report report;
    // Name-only materials: MaterialNames/Materials stay empty and the consuming project supplies
    // them, exactly as before this moved onto modelio (see this file's header comment on why
    // embedding was abandoned here). The 36-byte indexed layout is the same one
    // fixedMeshSpecification() hands the live GPU model.
    if (!modelio::writeMppModelWithNamedMaterials(toModelData(built, materialLibrary), fixedMeshSpecification(),
                                                  utf8Path, report)) {
      if (outError) {
        *outError = report.format();
        if (outError->empty()) *outError = "the model could not be written";
      }
      return false;
    }
    return true;
  } catch (const std::exception& error) {
    if (outError) *outError = error.what();
    return false;
  }
}

}  // namespace modeltool
