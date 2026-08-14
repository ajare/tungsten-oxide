// VertexPacking.hpp — ModelData -> the interleaved byte layout a MeshSpecification describes.
//
// This is where "can this mesh be converted to the MeshSpecification?" is actually answered: a
// component the spec names that this converter cannot supply, or a datatype it cannot encode, is
// reported as an error rather than silently zero-filled.
//
// Only a single vertex-buffer layout is supported. Multi-buffer specifications are legal in mpp
// and are rejected here with a diagnostic rather than half-handled -- nothing in this repository
// authors one, and guessing which buffer a channel belongs in would be inventing a contract.
#pragma once

#include <cstdint>
#include <vector>

#include <mpp/mesh/MeshSpecification.h>

#include "modelio/Diagnostics.hpp"
#include "modelio/ModelData.hpp"

namespace modelio {

struct PackedMesh {
  std::vector<std::int8_t> vertexBytes;
  std::size_t vertexCount{0};
  // Empty when the target specification is non-indexed -- in that case the vertex stream has
  // already been expanded through the source indices, so there is nothing left to index.
  std::vector<std::uint32_t> indices;
};

// Returns false (having reported) if the mesh cannot be expressed in `spec`. `mesh` must already
// carry every channel `spec` names -- synthesising the missing ones is the caller's job, so that
// it can report what was invented (see GltfConvert.hpp's validation pass).
bool packMesh(const MeshData& mesh, const mpp::mesh::MeshSpecification& spec, PackedMesh& out, Report& report);

}  // namespace modelio
