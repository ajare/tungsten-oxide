// MppModelIo.hpp — ModelData <-> .mppmodel, through mpp::ModelSerializer.
//
// Unlike cpp/editor's retired from-scratch byte writer, this drives the real serializer, so the
// layout is whatever the target MeshSpecification says rather than one hard-coded 36-byte form.
//
// KNOWN LIMITATION (non-indexed output). ModelSerializer offers no way to say "this mesh has no
// index stream": setMeshCount() value-initialises Mesh::indexStream to 0, and setIndexBuffer() is
// the only thing that ever changes it. A non-indexed model therefore records index-stream id 0
// while carrying no index streams at all. Readers that consult the material's MeshSpecification
// before touching indices -- cpp/tungsten-monoxide's Map.cpp, and readMppModel() below -- handle
// that correctly. mpp::MppModelStream does not: it calls getIndexData()/getIndexWidth()
// unconditionally. This matches the convention cpp/editor's shipped track exports already use, so
// it is a pre-existing property of non-indexed .mppmodel files rather than something introduced
// here, but it does mean a non-indexed target material produces a model MppModelStream cannot load.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <mpp/ResourceStream.h>
#include <mpp/mesh/MeshSpecification.h>

#include "modelio/Diagnostics.hpp"
#include "modelio/ModelData.hpp"
#include "modelio/PipelineMaterial.hpp"

namespace modelio {

// Writes every mesh in `model`, packed to `target`'s MeshSpecification, with one embedded
// PbrMaterial per entry in `model.materials`. Texture paths are resolved relative to the output
// file's own directory. Returns false having reported; no partial file is left behind.
bool writeMppModel(const ModelData& model, const TargetMaterial& target, const std::filesystem::path& outPath,
                   Report& report);

// Read-back, for verification and round-trip tests. Geometry only: materials are reported by name,
// not deserialized, since checking that the bytes we wrote come back is the point.
struct ReadMesh {
  std::string name;
  std::string material;
  std::size_t vertexCount{0};
  std::size_t vertexStride{0};
  std::size_t primitiveCount{0};
  std::vector<std::int8_t> vertexBytes;
  std::vector<std::uint32_t> indices;  // empty when the model is non-indexed
};

struct ReadModel {
  std::vector<ReadMesh> meshes;
  std::vector<std::string> materialNames;
  // The deserialized embedded material streams, parallel to materialNames. Exposed so a caller can
  // verify what actually survived serialization (texture bindings, colour space, surface factors)
  // rather than trusting that it did.
  std::vector<mpp::ResourceStreamPtr> materials;
};

// `indexed` must match the target specification the file was written against -- see the limitation
// above; the file itself does not record it. Throws std::runtime_error on an unreadable file.
ReadModel readMppModel(const std::filesystem::path& path, bool indexed);

}  // namespace modelio
