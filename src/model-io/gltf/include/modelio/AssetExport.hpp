// AssetExport.hpp — ModelData -> glTF/GLB, the inverse of AssetImport.hpp.
//
// Builds an in-memory aiScene from a modelio::ModelData and writes it with AssImp's own glTF2
// exporter (registered as "gltf2"/"glb2" in Assimp::Exporter -- already compiled into this
// project's vendored AssImp with no build changes: ASSIMP_NO_EXPORT is never set). Material
// factors/textures are written through the same AI_MATKEY_* keys AssetImport.cpp reads on the way
// in, so a round trip through this pair is lossless for everything ModelData itself can represent.
//
// Draco compression is deliberately not offered: this vendored AssImp can decode
// KHR_draco_mesh_compression on import but has no write support for it in its glTF2 exporter, and
// patching that is out of scope for this module.
#pragma once

#include <filesystem>

#include "modelio/Diagnostics.hpp"
#include "modelio/ModelData.hpp"

namespace modelio {

struct ExportOptions {
  bool binary{false};  // false = .gltf text (+ external images/.bin), true = .glb (embedded)
};

// Returns false having reported. On failure no output file is left behind.
bool exportAsset(const ModelData& model, const std::filesystem::path& outPath, const ExportOptions& options,
                 Report& report);

}  // namespace modelio
