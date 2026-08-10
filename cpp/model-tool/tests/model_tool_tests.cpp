// model_tool_tests.cpp — headless tests for NormalSmoothing.cpp/ObjSmoothingGroups.cpp
// (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 4.1/4.2), the pieces of model_tool that need neither
// AssImp nor a live GPU/window to exercise. ObjSmoothingGroups.cpp in particular is deliberately
// AssImp-free too (it re-parses the raw .obj text itself -- see its own header comment on why), so
// even the smoothing-group extraction is testable here without a real AssImp import. The old
// CollidableFlag.cpp round-trip coverage that used to live here was removed along with
// CollidableFlag.hpp itself (TRACK_MODEL_LIST_PLAN.md Milestone 3.2) -- see model_xml_tests.cpp for
// the Type/Visible metadata this replaced it with.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "NormalSmoothing.hpp"
#include "ObjSmoothingGroups.hpp"
#include "OpenTarget.hpp"

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

// OpenTarget.cpp tests (TRACK_MODEL_LIST_PLAN.md Milestone 3.3): classification across the three
// Open-dialog input shapes, Track-resource Models-list scanning, and rewriting one embedded <Model>
// in place without disturbing the rest of the document.
void writeFile(const std::filesystem::path& path, const std::string& contents) {
  std::ofstream out(path, std::ios::binary);
  out << contents;
}

void testClassifyOpenTarget() {
  const std::filesystem::path dir = std::filesystem::temp_directory_path();

  check(modeltool::classifyOpenTarget(dir / "cube.mppmodel") == modeltool::OpenTargetKind::MppModel,
        ".mppmodel classifies by extension alone, no content read");

  const std::filesystem::path standalonePath = dir / "open_target_standalone.xml";
  writeFile(standalonePath, "<Model><ModelFile>Cube.mppmodel</ModelFile><Meshes>"
                            "<Mesh><Name>main</Name><Type>Physical</Type><Visible>true</Visible></Mesh>"
                            "</Meshes></Model>");
  check(modeltool::classifyOpenTarget(standalonePath) == modeltool::OpenTargetKind::StandaloneModelXml,
        "a bare <Model> root classifies as StandaloneModelXml");

  const std::filesystem::path trackResourcePath = dir / "open_target_track_resource.xml";
  writeFile(trackResourcePath,
            "<Resources><Namespace name=\"Tracks\"><Resource type=\"Track\" name=\"T\"><Definitions>"
            "<Definition factory=\"Track\"><Models>"
            "<Model id=\"m1\"><ModelFile>Cube.mppmodel</ModelFile><Meshes>"
            "<Mesh><Name>main</Name><Type>Physical</Type><Visible>true</Visible></Mesh>"
            "</Meshes></Model>"
            "</Models></Definition></Definitions></Resource></Namespace></Resources>");
  check(modeltool::classifyOpenTarget(trackResourcePath) == modeltool::OpenTargetKind::TrackResourceXml,
        "a Track resource with a Definition[factory=Track]/Models list classifies as TrackResourceXml");

  const std::filesystem::path emptyResourcesPath = dir / "open_target_empty_resources.xml";
  writeFile(emptyResourcesPath, "<Resources></Resources>");
  check(modeltool::classifyOpenTarget(emptyResourcesPath) == modeltool::OpenTargetKind::Unsupported,
        "a Resources document with no Track Models list is Unsupported, not TrackResourceXml");

  check(modeltool::classifyOpenTarget(dir / "cube.obj") == modeltool::OpenTargetKind::Unsupported,
        "a non-.xml, non-.mppmodel extension is Unsupported (falls through to AssImp)");

  std::remove(standalonePath.string().c_str());
  std::remove(trackResourcePath.string().c_str());
  std::remove(emptyResourcesPath.string().c_str());
}

void testScanAndRewriteTrackResourceModels() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "open_target_scan_rewrite.xml";
  writeFile(path,
            "<Resources><Namespace name=\"Tracks\"><Resource type=\"Track\" name=\"T\"><Definitions>"
            "<Definition factory=\"Track\"><Models>"
            "<Model id=\"track-model\"><ModelFile>New_Track.mppmodel</ModelFile>"
            "<TrackData>New_Track.json</TrackData><Meshes>"
            "<Mesh><Name>road</Name><Type>Track</Type><Visible>true</Visible></Mesh>"
            "</Meshes></Model>"
            "<Model id=\"cube-model\"><ModelFile>Cube.mppmodel</ModelFile><Meshes>"
            "<Mesh><Name>main</Name><Type>Physical</Type><Visible>true</Visible></Mesh>"
            "</Meshes></Model>"
            "</Models></Definition></Definitions></Resource>"
            "<!-- an unrelated marker comment, expected to survive rewriteEmbeddedModel untouched -->"
            "</Namespace></Resources>");

  const std::vector<modeltool::TrackResourceModelEntry> entries = modeltool::scanTrackResourceModels(path);
  check(entries.size() == 2, "both embedded Model entries are found by the scan");
  check(entries.size() >= 1 && entries[0].id == "track-model" && entries[0].modelFileReference == "New_Track.mppmodel",
        "the first entry's id/modelFileReference match the source XML");
  check(entries.size() >= 2 && entries[1].id == "cube-model", "the second entry's id matches the source XML");

  const modelxml::ModelXmlDefinition trackModel = modeltool::readEmbeddedModel(path, "track-model");
  check(trackModel.trackData.has_value() && *trackModel.trackData == "New_Track.json", "readEmbeddedModel returns full mesh/TrackData metadata");
  check(trackModel.meshes.size() == 1 && trackModel.meshes[0].type == modelxml::MeshType::Track, "readEmbeddedModel's mesh Type round-trips");

  bool readThrewOnMissingId = false;
  try {
    modeltool::readEmbeddedModel(path, "does-not-exist");
  } catch (const std::exception&) {
    readThrewOnMissingId = true;
  }
  check(readThrewOnMissingId, "readEmbeddedModel throws when the target id isn't found");

  modelxml::ModelXmlDefinition rewritten;
  rewritten.id = "cube-model";
  rewritten.modelFile = "Cube.mppmodel";
  rewritten.meshes.push_back({"main", modelxml::MeshType::Decorative, false});
  modeltool::rewriteEmbeddedModel(path, "cube-model", rewritten);

  const std::vector<modeltool::TrackResourceModelEntry> afterRewrite = modeltool::scanTrackResourceModels(path);
  check(afterRewrite.size() == 2, "rewriteEmbeddedModel changes neither entry's presence");
  check(afterRewrite.size() >= 1 && afterRewrite[0].id == "track-model" &&
            afterRewrite[0].modelFileReference == "New_Track.mppmodel",
        "the untouched Model entry survives rewriteEmbeddedModel exactly as it was");

  std::ifstream reread(path);
  const std::string contents((std::istreambuf_iterator<char>(reread)), std::istreambuf_iterator<char>());
  check(contents.find("unrelated marker comment") != std::string::npos,
        "an unrelated XML comment elsewhere in the document survives rewriteEmbeddedModel");
  check(contents.find("Decorative") != std::string::npos, "the rewritten Model's new Type is actually on disk");

  bool threwOnMissingId = false;
  try {
    modeltool::rewriteEmbeddedModel(path, "does-not-exist", rewritten);
  } catch (const std::exception&) {
    threwOnMissingId = true;
  }
  check(threwOnMissingId, "rewriteEmbeddedModel throws when the target id isn't found");

  std::remove(path.string().c_str());
}

}  // namespace

int main() {
  testNormalsHardEdgeAcrossMeshesWithoutGroups();
  testNormalsSmoothAcrossMeshesWithMatchingGroups();
  testNormalsSplitVertexAtGroupBoundary();
  testExtractObjSmoothingGroups();
  testClassifyOpenTarget();
  testScanAndRewriteTrackResourceModels();

  if (failures) {
    std::cerr << failures << " model_tool test(s) failed\n";
    return 1;
  }
  std::cout << "PASS: model_tool NormalSmoothing/ObjSmoothingGroups\n";
  return 0;
}
