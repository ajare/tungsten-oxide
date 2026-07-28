// AssImpImport.hpp — AssImp (OBJ/FBX/USD/glTF) -> ImportedModel, model-tool's own fixed-layout
// in-memory representation. Bespoke, AssImp-inspired conversion code (NOT a reuse of
// ext/massivepolypusher/model-convert's AssImpModelLoader, which is built around an externally
// specified, arbitrary vertex layout via ModelspecStream/MeshSpecification -- model-tool only ever
// targets one fixed layout, so that indirection buys nothing here). See
// docs/adr/0001-model-tool.md, decisions D3/D4.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace modeltool {

// The one fixed interleaved vertex layout model-tool always uses: position (float x3), normal
// (float x3), uv (float x2), colour (unorm8 x4) -- 36 bytes/vertex, matching
// MPPMODEL_EXPORT_SPEC.md 4.1's recommended layout. Colour defaults to opaque white when a source
// mesh has no vertex-colour channel; uv defaults to (0,0) when it has no texture-coordinate
// channel -- both matching cpp/core's RenderVertex/GeometryBatch conventions.
struct ImportedVertex {
  float px{0}, py{0}, pz{0};
  float nx{0}, ny{0}, nz{0};
  float u{0}, v{0};
  std::uint8_t r{255}, g{255}, b{255}, a{255};
};

// A material's only extracted metadata is its diffuse/base-color texture path (see ADR 0001 D4) --
// nullopt when the source material has no usable (non-embedded) diffuse/base-color texture, in
// which case the default-white sentinel material is used instead (D7).
struct ImportedMaterial {
  std::string name;
  std::optional<std::string> diffuseTexturePath;  // absolute filesystem path when present
  // Set when a material *did* reference a texture but it was embedded in the source file (common
  // in .glb) rather than an external file -- embedded textures are skipped for v1 (ADR 0001 D4).
  // Surfaced so the caller can report it rather than silently treating it like "no texture".
  bool skippedEmbeddedTexture{false};
};

struct ImportedMesh {
  std::string name;
  std::vector<ImportedVertex> vertices;
  std::vector<std::uint32_t> indices;  // triangle list, real shared-vertex indices from AssImp
  int materialIndex{0};                 // index into ImportedModel::materials (always valid -- see below)
};

struct ImportedModel {
  std::string sourcePath;
  std::vector<ImportedMesh> meshes;
  // Always has at least one entry, even for a source scene with no materials at all -- a mesh's
  // materialIndex always safely indexes into this (importModel() appends a default untextured
  // entry when the source scene has none, and clamps any out-of-range index to it).
  std::vector<ImportedMaterial> materials;
};

// Loads `path` (OBJ/FBX/USD/glTF, dispatched by AssImp on content/extension) and converts it into
// the fixed layout above. Node-hierarchy transforms are baked into vertex data at import time
// (aiProcess_PreTransformVertices) since .mppmodel has no node concept to preserve them in (ADR
// 0001 D3). Returns nullopt and fills `outError` on failure (unreadable/unsupported file, or a
// scene with no mesh data).
std::optional<ImportedModel> importModel(const std::string& utf8Path, std::string* outError);

}  // namespace modeltool
