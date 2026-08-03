// NormalSmoothing.hpp — recomputes every vertex normal in an ImportedModel from triangle winding
// order, smoothly *across* sub-mesh boundaries (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 4.2):
// vertices from different ImportedMesh entries that share a position are treated as one shared
// vertex for normal-averaging purposes, exactly like a smoothing group that spans the whole model
// rather than one confined to a single mesh. This deliberately never trusts the source file's own
// normals (AssImp's per-mesh aiProcess_GenSmoothNormals included) -- the whole point is a
// consistent, seamless result across a multi-sub-mesh drivable object where sub-mesh boundaries are
// an authoring/organizational artifact, not a real crease.
//
// Deliberately kept free of any AssImp/mpp dependency -- it operates purely on
// AssImpImport.hpp's plain data types -- both so it's usable from MppModelImport.cpp's round-trip
// path too (not just importModel()) and so it's cheaply unit-testable headlessly (see
// tests/model_tool_tests.cpp), unlike the rest of this app's AssImp/mpp-fronted code.
#pragma once

#include "AssImpImport.hpp"

namespace modeltool {

// Mutates every mesh's vertex normals in place. No smoothing-angle threshold: every triangle
// touching a shared position contributes, matching AssImp's own GenSmoothNormals default (no
// crease-angle configured) -- just extended across sub-mesh boundaries by matching vertex
// positions across ALL of `model.meshes`, not only within one mesh.
void recomputeSmoothNormalsAcrossMeshes(ImportedModel& model);

}  // namespace modeltool
