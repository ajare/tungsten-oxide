// Tangents.hpp — derivation of the two geometric channels a source asset may omit.
//
// Both operate on ModelData in place and preserve vertex ordering and count; indices are read only
// to decide which vertices share a frame. The tangent convention is deliberately identical to the
// one src/tungsten-monoxide's addPbrTangents established -- accumulate per-triangle tangent and
// bitangent, orthonormalise the tangent against the vertex normal, and store handedness in w --
// because the game's 52-byte contract and its existing content were produced under it.
#pragma once

#include "modelio/ModelData.hpp"

namespace modelio {

// Area-weighted smooth normals from the triangle list. Degenerate triangles contribute nothing; a
// vertex touched only by degenerate triangles keeps the (0,1,0) default.
void generateSmoothNormals(MeshData& mesh);

// Requires positions, normals and UVs to already be present and finite. A vertex whose accumulated
// tangent is unusable (no UV gradient, mirrored seam collapsing to zero) falls back to an arbitrary
// vector orthogonal to its normal, so the output is always an orthonormal frame.
void generateTangents(MeshData& mesh);

}  // namespace modelio
