// AssetImport.hpp — AssImp -> ModelData.
//
// Named for AssImp rather than glTF because AssImp dispatches on content: the same walk serves
// OBJ, FBX and USD, and src/model-tool routes all of them through here
// (docs/GLTF_IMPORT_PLAN.md, M2). glTF is simply the format the surrounding conversion pipeline
// was built for.
//
// Loading only: this produces the renderer-neutral model and reports what the source did and did
// not contain. It does not synthesise missing channels or judge material features against a
// pipeline -- that is GltfConvert.hpp's validation pass, which needs the target MeshSpecification.
//
// The scene graph is walked manually rather than flattened with aiProcess_PreTransformVertices
// (docs/adr/0004-gltf-import.md, D6): one output mesh per node x mesh instance, the accumulated
// world transform baked into positions and the inverse-transpose applied to normals and tangents,
// each mesh named from its node. Bare PreTransformVertices bakes transforms but merges meshes by
// material, which would destroy the per-mesh names model_xml's Type/Visible metadata is keyed on.
#pragma once

#include <filesystem>
#include <optional>

#include "modelio/Diagnostics.hpp"
#include "modelio/ModelData.hpp"

namespace modelio {

// What to do about an image packed inside the container (a .glb chunk, or a data: URI) rather than
// sitting beside it as a file.
enum class EmbeddedTexturePolicy {
  // An embedded material can only reference a texture by path, so there is nothing sensible to
  // write -- refuse the import (docs/adr/0004-gltf-import.md, D5). Used by glTF -> .mppmodel
  // conversion.
  Reject,
  // Drop the binding, flag it on the material and carry on. Used by src/model-tool, which
  // references materials by name and only wants a path for preview, so a missing one costs it a
  // placeholder texture rather than a broken asset (ADR 0001, D4).
  Skip,
};

struct ImportOptions {
  // Set from the target MeshSpecification: an indexed target wants AssImp to weld identical
  // vertices, a non-indexed one gains nothing from it since the stream is expanded anyway.
  bool joinIdenticalVertices{false};
  EmbeddedTexturePolicy embeddedTextures{EmbeddedTexturePolicy::Reject};
  // Drop materials no mesh references. AssImp's glTF2 importer appends a default material to every
  // scene whether or not anything uses it, and a UI that lists materials should not show it.
  bool pruneUnreferencedMaterials{false};
};

// Returns nullopt (having reported) for an unreadable/unsupported file, a scene with no triangle
// meshes, or -- under EmbeddedTexturePolicy::Reject -- a container-packed image.
std::optional<ModelData> importAsset(const std::filesystem::path& path, const ImportOptions& options, Report& report);

}  // namespace modelio
