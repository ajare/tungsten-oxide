// MppModelImport.hpp — .mppmodel binary import: a from-scratch, read-only geometry reader, the
// mirror image of MppModelExport.cpp's from-scratch writer (TRACK_MODEL_LIST_PLAN.md Milestone 4,
// docs/adr/0003-model-xml-layer.md). Reads vertex positions/normals/UVs and per-mesh names/triangle
// counts only -- no materials, no shaders, no mpp::ModelSerializer/mpp::ResourceManager involvement
// -- so the editor can render a placement's referenced model in its own OpenGL viewport without
// linking any part of the MassivePolyPusher SDK (see MppModelExport.hpp's header comment on why
// that link is avoided, and 0003's "why the editor gets its own from-scratch reader" section).
//
// The on-disk format is verified field-for-field against the real
// ext/massivepolypusher/mpp/src/ModelSerializer.cpp read*/write* pairs (the single source of truth
// -- MPPMODEL_EXPORT_SPEC.md is referenced elsewhere in this codebase but not present in-repo), so
// this reads back both MppModelExport.cpp's own non-indexed output AND a real indexed file written
// by model-tool's mpp::ModelSerializer::save(). Materials/MaterialNames sections are Directory-
// skipped, never parsed -- they're ResourceStreamSerializer's own vendored, more complex format,
// which this reader has no need to understand for geometry alone.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Vec3.hpp"

namespace editor {

struct MppModelVertex {
  tox::Vec3 position;
  tox::Vec3 normal;
  float u{0.0f}, v{0.0f};
};

struct ImportedMppMesh {
  std::string name;
  std::string material;  // name only, as read -- never resolved to a real Material resource here
  std::vector<MppModelVertex> vertices;
  // Triangle-list indices into `vertices`, always populated (0,1,2,... when the mesh's own
  // indexStream is the "no index stream" sentinel, matching MppModelExport.cpp's own non-indexed
  // convention) -- so a caller never needs to branch on whether the source file was indexed.
  std::vector<std::uint32_t> indices;
};

struct ImportedMppModel {
  std::vector<ImportedMppMesh> meshes;
};

// Throws std::runtime_error on a bad magic/directory/section, an unsupported vertex stride (this
// reader -- like model-tool's own MppModelImport.cpp -- only supports the fixed 36-byte Position3+
// Normal3+TexCoord2+Colour4 layout both writers in this codebase always use), or more than one
// vertex/index stream per mesh (neither writer in this codebase ever produces that).
ImportedMppModel readMppModelGeometry(const std::filesystem::path& path);

}  // namespace editor
