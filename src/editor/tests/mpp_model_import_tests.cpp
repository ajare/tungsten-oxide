// mpp_model_import_tests.cpp — headless tests for MppModelImport.cpp/MppModelExport.cpp
// (TRACK_MODEL_LIST_PLAN.md Milestone 4, docs/GLTF_IMPORT_PLAN.md M4): round-trips
// MppModelExport.cpp's own non-indexed, 52-byte PBR output through the real
// modelio::writeMppModelWithNamedMaterials/mpp::ModelSerializer pair; hand-writes minimal real
// INDEXED .mppmodel files byte-for-byte per
// ext/willpower/ext/massive-poly-pusher/mpp/src/ModelSerializer.cpp's
// actual write*() functions, in both the 36-byte legacy and 52-byte PBR layouts, to prove
// readMppModelGeometry's indexed/non-indexed detection (MppModelImport.cpp's mppModelIsIndexed) is
// correct for both -- the one piece of this reader that isn't just "call
// mpp::ModelSerializer/modelio::readMppModel and trust it", since neither of those knows a file's
// indexedness on their own (see modelio/MppModelIo.hpp's header comment).
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "MppModelExport.hpp"
#include "MppModelImport.hpp"
#include "Track.hpp"
#include "modelio/MeshLayout.hpp"

namespace {

int failures = 0;
void check(bool condition, const std::string& message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

void checkClose(double got, double want, const std::string& message) {
  check(std::fabs(got - want) < 1e-4, message + ": got " + std::to_string(got) + ", want " + std::to_string(want));
}

void writeFile(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// --- Minimal hand-written-per-spec .mppmodel byte builder (mirrors ModelSerializer.cpp's write*
// functions exactly, INDEXED this time -- MppModelExport.cpp never writes an index stream, so this
// is the only way to exercise that path without a live model-tool session in this environment). ---

void appendU16(std::string& out, std::uint16_t v) { out.append(reinterpret_cast<const char*>(&v), sizeof(v)); }
void appendU32(std::string& out, std::uint32_t v) { out.append(reinterpret_cast<const char*>(&v), sizeof(v)); }
void appendF32(std::string& out, float v) { out.append(reinterpret_cast<const char*>(&v), sizeof(v)); }
void appendString(std::string& out, const std::string& s) {
  appendU32(out, static_cast<std::uint32_t>(s.size()));
  out += s;
}

// `pbr` selects between the 36-byte legacy layout and the 52-byte PBR layout (+tangent4, an
// arbitrary-but-finite placeholder here since these tests only assert position/normal round-trip)
// -- docs/GLTF_IMPORT_PLAN.md M4's dual-stride acceptance.
std::string buildIndexedSquareMppModel(bool pbr) {
  // One "square" mesh: 4 vertices (a unit quad in the XY plane), 6 indices (two triangles), 16-bit
  // index width -- exercises both the real IndexData section and 16-bit index unpacking.
  std::string vertexData;
  const float positions[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  for (const auto& p : positions) {
    appendF32(vertexData, p[0]);
    appendF32(vertexData, p[1]);
    appendF32(vertexData, p[2]);
    appendF32(vertexData, 0.0f);
    appendF32(vertexData, 0.0f);
    appendF32(vertexData, 1.0f);  // normal +Z
    appendF32(vertexData, 0.0f);
    appendF32(vertexData, 0.0f);  // uv
    const std::uint8_t rgba[4] = {255, 255, 255, 255};
    vertexData.append(reinterpret_cast<const char*>(rgba), 4);
    if (pbr) {
      appendF32(vertexData, 1.0f);
      appendF32(vertexData, 0.0f);
      appendF32(vertexData, 0.0f);
      appendF32(vertexData, 1.0f);  // tangent4 (xyz + handedness)
    }
  }

  std::string vertexDataSection;
  appendU32(vertexDataSection, static_cast<std::uint32_t>(vertexData.size()));
  appendU32(vertexDataSection, 4);                // vertexCount
  appendU32(vertexDataSection, pbr ? 52u : 36u);  // vertexStride
  vertexDataSection += vertexData;

  const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
  std::string indexData;
  for (std::uint16_t i : indices) appendU16(indexData, i);
  std::string indexDataSection;
  appendU32(indexDataSection, static_cast<std::uint32_t>(indexData.size()));
  appendU32(indexDataSection, 16);  // indexWidth
  indexDataSection += indexData;

  std::string meshMetadataSection;
  appendString(meshMetadataSection, "square");
  appendU32(meshMetadataSection, 2);  // primitiveType: Triangles
  appendU32(meshMetadataSection, 2);  // primitiveCount: 2 triangles
  appendString(meshMetadataSection, "Tracks/DefaultMeshMaterial");
  appendU32(meshMetadataSection, 1);  // numVertexBuffers
  appendU32(meshMetadataSection, 0);  // vertexBufferId 0
  appendU32(meshMetadataSection, 0);  // indexStreamId 0 (not the "no index" sentinel)

  constexpr std::uint32_t kHeaderBytes = 12;
  constexpr std::uint32_t kDirectoryBytes = 6 * 16;
  std::uint32_t cursor = kHeaderBytes + kDirectoryBytes;
  auto makeEntry = [&](std::uint32_t type, const std::string& section, std::uint32_t count) {
    const std::uint32_t start = cursor;
    cursor += static_cast<std::uint32_t>(section.size());
    std::string entry;
    appendU32(entry, type);
    appendU32(entry, start);
    appendU32(entry, cursor);
    appendU32(entry, count);
    return entry;
  };

  std::string unusedEntry;
  appendU32(unusedEntry, 0);
  appendU32(unusedEntry, 0);
  appendU32(unusedEntry, 0);
  appendU32(unusedEntry, 0);
  const std::string materialNamesEntry = makeEntry(1, "", 0);
  const std::string materialsEntry = makeEntry(2, "", 0);
  const std::string vertexDataEntry = makeEntry(3, vertexDataSection, 1);
  const std::string indexDataEntry = makeEntry(4, indexDataSection, 1);
  const std::string meshMetadataEntry = makeEntry(5, meshMetadataSection, 1);

  std::string file;
  file += "MPPM";
  appendU16(file, 1);
  appendU16(file, 1);
  appendU32(file, 0x0001);  // FLAG_INDEXED_VERTICES, matching a real ModelSerializer::save()
  file += unusedEntry;
  file += materialNamesEntry;
  file += materialsEntry;
  file += vertexDataEntry;
  file += indexDataEntry;
  file += meshMetadataEntry;
  file += vertexDataSection;
  file += indexDataSection;
  file += meshMetadataSection;
  return file;
}

void testRoundTripEditorWrittenFile() {
  tox::Track track;
  tox::GeometryBatch batch;
  batch.id = "triangle-mesh";
  batch.materialKey = "road";
  tox::RenderVertex v0, v1, v2;
  v0.position = tox::Vec3(0, 0, 0);
  v1.position = tox::Vec3(1, 0, 0);
  v2.position = tox::Vec3(0, 1, 0);
  v0.normal = v1.normal = v2.normal = tox::Vec3(0, 0, 1);
  batch.vertices = {v0, v1, v2};
  track.geometry.push_back(batch);

  const editor::MppModelExportResult exported = editor::exportTrackToMppModel(track);
  check(exported.meshCount == 1, "exportTrackToMppModel produces one mesh");

  const std::filesystem::path path = std::filesystem::temp_directory_path() / "mpp_model_import_tests_editor_written.mppmodel";
  writeFile(path, exported.bytes);

  const editor::ImportedMppModel imported = editor::readMppModelGeometry(path);
  check(imported.meshes.size() == 1, "readMppModelGeometry reads back one mesh from an editor-written file");
  if (imported.meshes.size() == 1) {
    const editor::ImportedMppMesh& mesh = imported.meshes.front();
    check(mesh.name == "triangle-mesh", "mesh name round-trips");
    check(mesh.vertices.size() == 3, "vertex count round-trips");
    check(mesh.indices.size() == 3 && mesh.indices[0] == 0 && mesh.indices[1] == 1 && mesh.indices[2] == 2,
          "a non-indexed mesh gets synthesized identity indices");
    if (mesh.vertices.size() == 3) {
      checkClose(mesh.vertices[1].position.x, 1.0, "vertex 1 position.x round-trips");
      checkClose(mesh.vertices[2].position.y, 1.0, "vertex 2 position.y round-trips");
      checkClose(mesh.vertices[0].normal.z, 1.0, "vertex 0 normal.z round-trips");
    }
  }
  std::remove(path.string().c_str());
}

void testRoundTripRealIndexedFile(bool pbr) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     (pbr ? "mpp_model_import_tests_indexed_square_pbr.mppmodel" : "mpp_model_import_tests_indexed_square.mppmodel");
  writeFile(path, buildIndexedSquareMppModel(pbr));

  const std::string label = pbr ? " (52-byte PBR layout)" : " (36-byte legacy layout)";
  const editor::ImportedMppModel imported = editor::readMppModelGeometry(path);
  check(imported.meshes.size() == 1, "readMppModelGeometry reads back one mesh from a real-format indexed file" + label);
  if (imported.meshes.size() == 1) {
    const editor::ImportedMppMesh& mesh = imported.meshes.front();
    check(mesh.name == "square", "mesh name round-trips" + label);
    check(mesh.material == "Tracks/DefaultMeshMaterial", "mesh material name round-trips" + label);
    check(mesh.vertices.size() == 4, "vertex count round-trips" + label);
    const std::vector<std::uint32_t> expectedIndices = {0, 1, 2, 0, 2, 3};
    check(mesh.indices == expectedIndices, "16-bit index stream unpacks to the expected triangle list" + label);
    if (mesh.vertices.size() == 4) {
      checkClose(mesh.vertices[2].position.x, 1.0, "vertex 2 position.x round-trips" + label);
      checkClose(mesh.vertices[3].position.y, 1.0, "vertex 3 position.y round-trips" + label);
    }
  }
  std::remove(path.string().c_str());
}

void testBadMagicThrows() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "mpp_model_import_tests_bad_magic.mppmodel";
  writeFile(path, "NOPE-not-a-real-mppmodel-file");
  bool threw = false;
  try {
    editor::readMppModelGeometry(path);
  } catch (const std::exception&) {
    threw = true;
  }
  check(threw, "a file with a bad magic number throws");
  std::remove(path.string().c_str());
}

// Pins the two layouts this reader/writer pair depends on against src/model-io's own definition of
// them (docs/GLTF_IMPORT_PLAN.md M4): position3/normal3/texcoord2/colour4 must sit at the fixed
// offsets both MppModelImport.cpp's unpackVertices and MppModelExport.cpp's tox::GeometryBatch
// packing assume, in both the 36-byte legacy layout (still read for already-committed track
// resources) and the 52-byte PBR layout (what the editor's track export now writes).
void testStrideMatchesModelIoContract() {
  const mpp::mesh::MeshSpecification legacy = modelio::gameMeshSpecification(/*indexed=*/false, /*pbr=*/false);
  check(legacy.getVertexStrideInBytes() == 36, "model_io's legacy layout is 36 bytes");
  check(modelio::LegacyPbrVertexStride == 36, "the published legacy stride constant agrees");

  const auto legacyPosition = modelio::findAttribute(legacy, mpp::mesh::Vertex::Component::Position3);
  const auto legacyNormal = modelio::findAttribute(legacy, mpp::mesh::Vertex::Component::Normal3);
  const auto legacyUv = modelio::findAttribute(legacy, mpp::mesh::Vertex::Component::TexCoord2);
  const auto legacyColour = modelio::findAttribute(legacy, mpp::mesh::Vertex::Component::Colour4);
  check(legacyPosition && legacyPosition->offsetInBytes == 0, "legacy position3 sits at offset 0");
  check(legacyNormal && legacyNormal->offsetInBytes == 12, "legacy normal3 sits at offset 12");
  check(legacyUv && legacyUv->offsetInBytes == 24, "legacy texcoord2 sits at offset 24");
  check(legacyColour && legacyColour->offsetInBytes == 32, "legacy colour4 sits at offset 32");
  check(legacyColour && legacyColour->normalised, "legacy colour4 is normalised unsigned bytes");

  const mpp::mesh::MeshSpecification pbr = modelio::gameMeshSpecification(/*indexed=*/false, /*pbr=*/true);
  check(pbr.getVertexStrideInBytes() == 52, "model_io's PBR layout is 52 bytes");
  check(modelio::PbrVertexStride == 52, "the published PBR stride constant agrees");

  const auto pbrPosition = modelio::findAttribute(pbr, mpp::mesh::Vertex::Component::Position3);
  const auto pbrNormal = modelio::findAttribute(pbr, mpp::mesh::Vertex::Component::Normal3);
  const auto pbrUv = modelio::findAttribute(pbr, mpp::mesh::Vertex::Component::TexCoord2);
  const auto pbrColour = modelio::findAttribute(pbr, mpp::mesh::Vertex::Component::Colour4);
  const auto pbrTangent = modelio::findAttribute(pbr, mpp::mesh::Vertex::Component::Tangent4);
  check(pbrPosition && pbrPosition->offsetInBytes == 0, "PBR position3 sits at offset 0");
  check(pbrNormal && pbrNormal->offsetInBytes == 12, "PBR normal3 sits at offset 12");
  check(pbrUv && pbrUv->offsetInBytes == 24, "PBR texcoord2 sits at offset 24");
  check(pbrColour && pbrColour->offsetInBytes == 32, "PBR colour4 sits at offset 32");
  check(pbrTangent && pbrTangent->offsetInBytes == 36, "PBR tangent4 sits at offset 36");
}

}  // namespace

int main() {
  testRoundTripEditorWrittenFile();
  testRoundTripRealIndexedFile(/*pbr=*/false);
  testRoundTripRealIndexedFile(/*pbr=*/true);
  testBadMagicThrows();
  testStrideMatchesModelIoContract();

  if (failures) {
    std::cerr << failures << " mpp_model_import test(s) failed\n";
    return 1;
  }
  std::cout << "PASS: editor .mppmodel geometry reader (legacy + PBR, non-indexed + indexed round trip)\n";
  return 0;
}
