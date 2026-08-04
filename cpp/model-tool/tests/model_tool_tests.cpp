// model_tool_tests.cpp — headless tests for NormalSmoothing.cpp/ObjSmoothingGroups.cpp/
// CollidableFlag.cpp (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 4.1/4.2), the pieces of model_tool
// that need neither AssImp nor a live GPU/window to exercise. ObjSmoothingGroups.cpp in particular
// is deliberately AssImp-free too (it re-parses the raw .obj text itself -- see its own header
// comment on why), so even the smoothing-group extraction is testable here without a real AssImp
// import.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include "CollidableFlag.hpp"
#include "NormalSmoothing.hpp"
#include "ObjSmoothingGroups.hpp"

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
  // Deliberately wrong/flat initial normals -- recomputeNormals must overwrite them from winding
  // order, never trust whatever's already there (matches the plan's "never trust the source file's
  // normals" decision).
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

// Two triangles from two SEPARATE meshes (simulating two sub-meshes AssImp never joins into one
// shared vertex buffer -- JoinIdenticalVertices only merges within one mesh) sharing an edge
// (P1, P2), each with its own independent copy of that edge's vertices. Mesh A: P0(0,0,0),
// P1(1,0,0), P2(0,1,0) -- flat in the XY plane, CCW face normal +Z. Mesh B: P1(1,0,0), P3(1,1,0.5),
// P2(0,1,0) -- a differently-tilted triangle sharing the same edge.
//
// With `groups = nullptr` (no smoothing-group data -- every format except OBJ, and a `.mppmodel`
// reimport), two DIFFERENT meshes must now get a HARD edge at that shared boundary: per-mesh mode
// was deliberately narrowed from Milestone 4.2's original across-every-mesh behavior once it became
// clear no format lacking real smoothing-group data ever authors a boundary that should stay
// seamless there.
void testNormalsHardEdgeAcrossMeshesWithoutGroups() {
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
  modeltool::recomputeNormals(model, nullptr);

  const modeltool::ImportedVertex& aP1 = model.meshes[0].vertices[1];
  const modeltool::ImportedVertex& bP1 = model.meshes[1].vertices[0];

  // Mesh A's own face normal is exactly (0,0,1) (flat in XY); mesh B's is tilted. Per-mesh mode
  // must leave each mesh's vertex at its OWN mesh's unsmoothed face normal, not the cross-mesh
  // average Milestone 4.2's original implementation would have produced.
  checkClose(aP1.nx, 0.0f, "mesh A's own P1 keeps mesh A's own face normal (no cross-mesh smoothing)");
  checkClose(aP1.ny, 0.0f, "mesh A's own P1 keeps mesh A's own face normal (no cross-mesh smoothing)");
  checkClose(aP1.nz, 1.0f, "mesh A's own P1 keeps mesh A's own face normal (no cross-mesh smoothing)");
  check(std::fabs(bP1.nx - aP1.nx) > 1e-3f || std::fabs(bP1.nz - aP1.nz) > 1e-3f,
        "mesh B's own P1 differs from mesh A's -- the boundary is hard, not smoothed");

  const float len = std::sqrt(aP1.nx * aP1.nx + aP1.ny * aP1.ny + aP1.nz * aP1.nz);
  checkClose(len, 1.0f, "normal is unit length");
}

// Same two meshes as above, but with EXPLICIT matching smoothing-group ids (both group 5) passed
// in -- real smoothing-group data is global across every mesh (an authored group spanning a
// material/mesh split must still smooth seamlessly), so this must reproduce Milestone 4.2's
// original cross-mesh-smooth behavior despite the vertices living in different ImportedMesh
// entries.
void testNormalsSmoothAcrossMeshesWithMatchingGroups() {
  modeltool::ImportedModel model;

  modeltool::ImportedMesh meshA;
  meshA.vertices = {vertexAt(0.0f, 0.0f, 0.0f), vertexAt(1.0f, 0.0f, 0.0f), vertexAt(0.0f, 1.0f, 0.0f)};
  meshA.indices = {0, 1, 2};
  modeltool::ImportedMesh meshB;
  meshB.vertices = {vertexAt(1.0f, 0.0f, 0.0f), vertexAt(1.0f, 1.0f, 0.5f), vertexAt(0.0f, 1.0f, 0.0f)};
  meshB.indices = {0, 1, 2};
  model.meshes = {meshA, meshB};

  std::vector<modeltool::MeshTriangleGroups> groups(2);
  groups[0].triangleGroup = {5};
  groups[1].triangleGroup = {5};
  modeltool::recomputeNormals(model, &groups);

  const modeltool::ImportedVertex& aP1 = model.meshes[0].vertices[1];
  const modeltool::ImportedVertex& bP1 = model.meshes[1].vertices[0];
  checkClose(aP1.nx, bP1.nx, "matching groups across meshes: normal.x smooths");
  checkClose(aP1.ny, bP1.ny, "matching groups across meshes: normal.y smooths");
  checkClose(aP1.nz, bP1.nz, "matching groups across meshes: normal.z smooths");
}

// One mesh, two triangles sharing an edge but in DIFFERENT smoothing groups: the shared vertices
// must be split into per-group copies (mesh.vertices grows from 4 to 6), each copy keeping its own
// group's unsmoothed-across-the-boundary normal -- a hard edge expressed via vertex duplication,
// since a single vertex can only ever carry one normal.
void testNormalsSplitVertexAtGroupBoundary() {
  modeltool::ImportedModel model;
  modeltool::ImportedMesh mesh;
  // P0(0,0,0), P1(1,0,0), P2(0,1,0) [group 1, flat +Z]; P1, P3(1,1,0.5), P2 [group 2, tilted].
  mesh.vertices = {vertexAt(0.0f, 0.0f, 0.0f), vertexAt(1.0f, 0.0f, 0.0f), vertexAt(0.0f, 1.0f, 0.0f), vertexAt(1.0f, 1.0f, 0.5f)};
  mesh.indices = {0, 1, 2, 1, 3, 2};
  model.meshes = {mesh};

  std::vector<modeltool::MeshTriangleGroups> groups(1);
  groups[0].triangleGroup = {1, 2};
  modeltool::recomputeNormals(model, &groups);

  check(model.meshes[0].vertices.size() == 6, "a vertex used by two different groups is split into two copies");
  check(model.meshes[0].indices.size() == 6, "index count is unchanged by splitting (still two triangles)");

  // The two triangles no longer share ANY vertex index post-split.
  const auto& indices = model.meshes[0].indices;
  const bool disjoint = indices[0] != indices[3] && indices[0] != indices[4] && indices[0] != indices[5] && indices[1] != indices[3] &&
                        indices[1] != indices[4] && indices[1] != indices[5] && indices[2] != indices[3] && indices[2] != indices[4] &&
                        indices[2] != indices[5];
  check(disjoint, "the two group's triangles reference entirely separate (post-split) vertex indices");

  const modeltool::ImportedVertex& p1Group1 = model.meshes[0].vertices[indices[1]];
  checkClose(p1Group1.nx, 0.0f, "group 1's copy of the shared vertex keeps group 1's own flat face normal");
  checkClose(p1Group1.nz, 1.0f, "group 1's copy of the shared vertex keeps group 1's own flat face normal");
}

void writeScratchObj(const std::string& path) {
  std::ofstream file(path);
  file << "v 0 0 0\n"
          "v 1 0 0\n"
          "v 0 1 0\n"
          "v 1 1 0.5\n"
          "s 1\n"
          "f 1 2 3\n"
          "s 2\n"
          "f 2 4 3\n";
}

// extractObjSmoothingGroups re-parses the raw .obj file directly (no AssImp involved), matching
// its faces back to an already-imported ImportedModel by vertex position. This builds the
// ImportedModel by hand (mirroring the shape AssImp's own Triangulate+JoinIdenticalVertices would
// produce for the same file) rather than actually running it through AssImp.
void testExtractObjSmoothingGroups() {
  const std::string path = "model_tool_tests_scratch.obj";
  writeScratchObj(path);

  modeltool::ImportedModel model;
  modeltool::ImportedMesh mesh;
  mesh.vertices = {vertexAt(0.0f, 0.0f, 0.0f), vertexAt(1.0f, 0.0f, 0.0f), vertexAt(0.0f, 1.0f, 0.0f), vertexAt(1.0f, 1.0f, 0.5f)};
  mesh.indices = {0, 1, 2, 1, 3, 2};
  model.meshes = {mesh};

  const auto groups = modeltool::extractObjSmoothingGroups(path, model);
  std::remove(path.c_str());

  check(groups.has_value(), "smoothing groups are extracted from a .obj file");
  if (groups.has_value()) {
    check(groups->size() == 1, "one MeshTriangleGroups entry per ImportedModel mesh");
    check((*groups)[0].triangleGroup.size() == 2, "one group id per triangle");
    check((*groups)[0].triangleGroup[0] == 1, "the first face's group matches its `s 1` declaration");
    check((*groups)[0].triangleGroup[1] == 2, "the second face's group matches its `s 2` declaration");
  }

  const auto notObj = modeltool::extractObjSmoothingGroups("scratch.fbx", model);
  check(!notObj.has_value(), "a non-.obj path returns nullopt (no smoothing-group data recoverable)");
}

}  // namespace

int main() {
  testCollidableFlagRoundTrip();
  testNormalsHardEdgeAcrossMeshesWithoutGroups();
  testNormalsSmoothAcrossMeshesWithMatchingGroups();
  testNormalsSplitVertexAtGroupBoundary();
  testExtractObjSmoothingGroups();

  if (failures) {
    std::cerr << failures << " model_tool test(s) failed\n";
    return 1;
  }
  std::cout << "PASS: model_tool NormalSmoothing/ObjSmoothingGroups/CollidableFlag\n";
  return 0;
}
