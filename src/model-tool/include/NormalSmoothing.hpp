// NormalSmoothing.hpp — recomputes every vertex normal in an ImportedModel from triangle winding
// order, never trusting the source file's own normals (AssImp-synthesized or otherwise).
//
// Two modes, chosen by whether real smoothing-group data is available for the source format (see
// ObjSmoothingGroups.hpp -- currently the only format this exists for is OBJ; AssImp's public
// `aiMesh` API exposes no smoothing-group data for any format, OBJ included, so every other format,
// and a `.mppmodel` reimport, always uses the second mode):
//
// - **Smoothing-group-aware** (`groups` passed non-null): two triangle corners are merged into one
//   smoothed normal only when they share BOTH a position AND a smoothing-group id -- global across
//   every mesh in the model, so an authored group spanning a `usemtl`/mesh split still smooths
//   seamlessly, exactly like the source file intended. Two adjacent faces in DIFFERENT groups get a
//   hard edge, even at a shared position -- which, since a single vertex can only ever carry one
//   normal, requires splitting that vertex into one copy per group it's used by (see below).
// - **Per-mesh** (`groups` is null): every triangle within one `ImportedMesh` shares one synthetic
//   group (so it's smooth throughout that one mesh); different meshes never smooth together. This
//   is what Milestone 4.2's original implementation did across ALL meshes (not just within one) --
//   deliberately narrowed once it became clear no cross-mesh smoothing-group is ever authored for
//   the formats that lack this data anyway, so cross-mesh smoothing there was papering over sub-mesh
//   boundaries the source file never asked to be seamless.
//
// Both modes may split vertices no other code here does today -- ImportedMesh's `vertices`/`indices`
// arrays can grow (never shrink) as a result of calling this.
#pragma once

#include <vector>

#include "AssImpImport.hpp"

namespace modeltool {

// One authored (or synthetic) smoothing-group id per triangle of one ImportedMesh, parallel to
// `mesh.indices.size() / 3`. Group ids are only ever compared for equality within one
// recomputeNormals() call -- their numeric value carries no meaning beyond that (in particular nothing
// requires OBJ's own `s <n>` numbers to be preserved verbatim, only that two faces authored under
// the same number, or both under no explicit `s`, end up with equal ids -- see ObjSmoothingGroups.cpp
// for how an explicit `s off`/`s 0` face instead gets a always-unique id, never merging with anything).
struct MeshTriangleGroups {
  std::vector<int> triangleGroup;
};

// `groups`, when non-null, must have exactly one entry per `model.meshes` entry (parallel), each
// sized to that mesh's own triangle count.
void recomputeNormals(ImportedModel& model, const std::vector<MeshTriangleGroups>* groups);

}  // namespace modeltool
