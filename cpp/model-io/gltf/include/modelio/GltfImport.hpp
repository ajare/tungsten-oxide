// GltfImport.hpp — AssImp (glTF/GLB) -> ModelData.
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

struct ImportOptions {
  // Set from the target MeshSpecification: an indexed target wants AssImp to weld identical
  // vertices, a non-indexed one gains nothing from it since the stream is expanded anyway.
  bool joinIdenticalVertices{false};
};

// Returns nullopt (having reported) for an unreadable/unsupported file, a scene with no triangle
// meshes, or any texture that is embedded in the container rather than an external file -- a .glb
// with packed images, or a data: URI, cannot be referenced by path from an embedded material
// (docs/adr/0004-gltf-import.md, D5).
std::optional<ModelData> importGltf(const std::filesystem::path& path, const ImportOptions& options, Report& report);

}  // namespace modelio
