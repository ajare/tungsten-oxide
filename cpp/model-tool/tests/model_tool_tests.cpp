// model_tool_tests.cpp — headless tests for NormalSmoothing.cpp/CollidableFlag.cpp
// (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 4.1/4.2), the two pieces of model_tool that need
// neither AssImp nor a live GPU/window to exercise.
#include <cmath>
#include <iostream>
#include <string>

#include "CollidableFlag.hpp"
#include "NormalSmoothing.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

void checkClose(float got, float want, const std::string& message) {
  check(std::fabs(got - want) < 1e-4f, message + ": got " + std::to_string(got) + ", want " + std::to_string(want));
}

modeltool::ImportedVertex vertexAt(float x, float y, float z) {
  modeltool::ImportedVertex v;
  v.px = x;
  v.py = y;
  v.pz = z;
  // Deliberately wrong/flat initial normals -- recomputeSmoothNormalsAcrossMeshes must overwrite
  // them from winding order, never trust whatever's already there (matches the plan's "never trust
  // the source file's normals" decision).
  v.nx = 0.0f;
  v.ny = 0.0f;
  v.nz = 1.0f;
  return v;
}

void testCollidableFlagRoundTrip() {
  check(modeltool::encodeCollidableInName("Ramp", true) == "Ramp", "collidable mesh name is written unchanged");
  check(modeltool::encodeCollidableInName("Ramp", false) == "Ramp~decorative", "decorative mesh name carries the marker");

  const modeltool::DecodedMeshName collidable = modeltool::decodeCollidableFromName("Ramp");
  check(collidable.name == "Ramp" && collidable.collidable, "a plain name decodes as collidable");

  const modeltool::DecodedMeshName decorative = modeltool::decodeCollidableFromName("Ramp~decorative");
  check(decorative.name == "Ramp" && !decorative.collidable, "a marked name decodes as decorative with the marker stripped");

  // A file this feature never touched (or a name that never had the marker) must decode as
  // collidable -- the least-surprising default for pre-existing content.
  const modeltool::DecodedMeshName untouched = modeltool::decodeCollidableFromName("Statue");
  check(untouched.name == "Statue" && untouched.collidable, "an untouched name still decodes as collidable");
}

// Two triangles from two SEPARATE meshes (simulating two sub-meshes that were never joined into
// one shared vertex buffer -- AssImp's JoinIdenticalVertices only merges within one mesh, never
// across sub-meshes) sharing an edge (P1, P2), each with its own independent copy of that edge's
// vertices. Mesh A: P0(0,0,0), P1(1,0,0), P2(0,1,0) -- flat in the XY plane, CCW face normal +Z.
// Mesh B: P1(1,0,0), P3(1,1,0.5), P2(0,1,0) -- a differently-tilted triangle sharing the same edge.
void testNormalsSmoothAcrossSubMeshBoundary() {
  modeltool::ImportedModel model;

  modeltool::ImportedMesh meshA;
  meshA.name = "MeshA";
  meshA.vertices = {vertexAt(0.0f, 0.0f, 0.0f), vertexAt(1.0f, 0.0f, 0.0f), vertexAt(0.0f, 1.0f, 0.0f)};
  meshA.indices = {0, 1, 2};

  modeltool::ImportedMesh meshB;
  meshB.name = "MeshB";
  meshB.vertices = {vertexAt(1.0f, 0.0f, 0.0f), vertexAt(1.0f, 1.0f, 0.5f), vertexAt(0.0f, 1.0f, 0.0f)};
  meshB.indices = {0, 1, 2};

  model.meshes = {meshA, meshB};
  modeltool::recomputeSmoothNormalsAcrossMeshes(model);

  const modeltool::ImportedVertex& aP1 = model.meshes[0].vertices[1];  // MeshA's own copy of P1
  const modeltool::ImportedVertex& bP1 = model.meshes[1].vertices[0];  // MeshB's own copy of P1
  const modeltool::ImportedVertex& aP2 = model.meshes[0].vertices[2];  // MeshA's own copy of P2
  const modeltool::ImportedVertex& bP2 = model.meshes[1].vertices[2];  // MeshB's own copy of P2

  checkClose(aP1.nx, bP1.nx, "shared vertex P1: normal.x continuous across sub-mesh boundary");
  checkClose(aP1.ny, bP1.ny, "shared vertex P1: normal.y continuous across sub-mesh boundary");
  checkClose(aP1.nz, bP1.nz, "shared vertex P1: normal.z continuous across sub-mesh boundary");
  checkClose(aP2.nx, bP2.nx, "shared vertex P2: normal.x continuous across sub-mesh boundary");
  checkClose(aP2.ny, bP2.ny, "shared vertex P2: normal.y continuous across sub-mesh boundary");
  checkClose(aP2.nz, bP2.nz, "shared vertex P2: normal.z continuous across sub-mesh boundary");

  const float len = std::sqrt(aP1.nx * aP1.nx + aP1.ny * aP1.ny + aP1.nz * aP1.nz);
  checkClose(len, 1.0f, "smoothed normal is unit length");

  // P0 is touched by exactly one triangle (mesh A's own), so its normal must be that triangle's
  // own unsmoothed face normal: cross((1,0,0), (0,1,0)) = (0,0,1).
  const modeltool::ImportedVertex& aP0 = model.meshes[0].vertices[0];
  checkClose(aP0.nx, 0.0f, "unshared vertex P0: normal.x matches its lone triangle's face normal");
  checkClose(aP0.ny, 0.0f, "unshared vertex P0: normal.y matches its lone triangle's face normal");
  checkClose(aP0.nz, 1.0f, "unshared vertex P0: normal.z matches its lone triangle's face normal");
}

}  // namespace

int main() {
  testCollidableFlagRoundTrip();
  testNormalsSmoothAcrossSubMeshBoundary();

  if (failures) {
    std::cerr << failures << " model_tool test(s) failed\n";
    return 1;
  }
  std::cout << "PASS: model_tool NormalSmoothing/CollidableFlag\n";
  return 0;
}
