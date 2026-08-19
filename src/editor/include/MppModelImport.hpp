// MppModelImport.hpp — .mppmodel binary import: a read-only geometry view, the mirror image of
// MppModelExport.cpp's writer. Reads vertex positions/normals/UVs and per-mesh names/triangle
// indices only -- no materials, no shaders -- so the editor's viewport can render a placement's
// referenced model without any of that model's own material machinery.
//
// docs/GLTF_IMPORT_PLAN.md M4 replaced this file's original from-scratch byte parser with a thin
// wrapper over src/model-io's modelio::readMppModel (itself backed by the real
// mpp::ModelSerializer), now that the editor links mpp (M3) -- see MppModelExport.hpp's header
// comment for why the no-mpp-dependency posture this file used to advertise was retired. One
// nuance model_io's own reader cannot resolve on its own: modelio::readMppModel(path, indexed)
// requires knowing up front whether the file's meshes carry a real index stream, because
// mpp::ModelSerializer::getIndexData()/getIndexWidth() index into an internal array with no bounds
// check and are undefined behaviour to call on a mesh that has none (modelio/MppModelIo.hpp's
// header comment: a non-indexed mpp::ModelSerializer-written file still leaves every mesh's
// internal indexStream field at its value-initialised 0, not a sentinel). This reader determines
// that once per file, up front, by peeking at the on-disk IndexData directory entry's own stream
// count -- see MppModelImport.cpp -- so it works for both the editor's own non-indexed 52-byte PBR
// track export and an indexed 36- or 52-byte model written by model-tool/gltf_convert.
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

// Throws std::runtime_error on a bad magic/directory/section, or a mesh using a vertex stride other
// than the 36-byte legacy or 52-byte PBR layout (modelio::LegacyPbrVertexStride/PbrVertexStride) --
// position3/normal3/texcoord2 sit at the same fixed offsets (0/12/24) in both, which is all this
// reader decodes.
ImportedMppModel readMppModelGeometry(const std::filesystem::path& path);

}  // namespace editor
