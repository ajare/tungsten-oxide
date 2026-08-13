// model_io_tests.cpp — headless tests for cpp/model-io (docs/GLTF_IMPORT_PLAN.md, M1).
//
// Covers the validation matrix (which missing channels are synthesised, which are fatal, which
// material features are refused) and a full round-trip of the written .mppmodel back through
// mpp::ModelSerializer, against the committed fixtures in cpp/test-data/fixtures/gltf.
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <mpp/BasicMaterialStream.h>
#include <mpp/PbrMaterialStream.h>
#include <mpp/TextureStream.h>
#include <mpp/resource-parsers/PbrPipelineDocumentLoader.h>

#include "modelio/GltfConvert.hpp"
#include "modelio/GltfImport.hpp"
#include "modelio/MeshLayout.hpp"
#include "modelio/MppModelIo.hpp"
#include "modelio/PipelineMaterial.hpp"

using namespace modelio;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

std::filesystem::path fixtures() { return std::filesystem::path(MODEL_IO_FIXTURE_DIR); }
std::filesystem::path fixture(const std::string& name) { return fixtures() / name; }

mpp::PbrPipelineDocument loadPipeline() {
  return mpp::resource_parsers::PbrPipelineDocumentLoader::fromFile(fixture("test.pipeline.xml").string());
}

// A scratch directory beside the fixtures would pollute a committed corpus, so outputs go to the
// build tree's temp area instead.
std::filesystem::path scratch() {
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "model_io_tests";
  std::filesystem::create_directories(directory);
  return directory;
}

float readFloat(const std::vector<std::int8_t>& bytes, std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

void testPipelineMaterialLookup() {
  const mpp::PbrPipelineDocument pipeline = loadPipeline();

  const std::vector<std::string> names = pipelineMaterialNames(pipeline);
  check(names.size() == 2, "fixture pipeline declares two PbrMaterials");

  Report report;
  const std::optional<TargetMaterial> indexed = findPipelineMaterial(pipeline, "Test.Indexed", report);
  check(indexed.has_value(), "Test.Indexed resolves");
  if (indexed) {
    check(indexed->meshSpec.verticesIndexed(), "Test.Indexed is indexed");
    check(indexed->meshSpec.getVertexStrideInBytes() == PbrVertexStride, "Test.Indexed is the 52-byte PBR layout");
    check(specHasComponent(indexed->meshSpec, mpp::mesh::Vertex::Component::Tangent4), "Test.Indexed declares tangent4");
  }

  const std::optional<TargetMaterial> flat = findPipelineMaterial(pipeline, "Test.Flat", report);
  check(flat.has_value(), "Test.Flat resolves");
  if (flat) {
    check(!flat->meshSpec.verticesIndexed(), "Test.Flat is non-indexed");
    check(flat->meshSpec.getVertexStrideInBytes() == LegacyPbrVertexStride, "Test.Flat is the 36-byte legacy layout");
    check(!specHasComponent(flat->meshSpec, mpp::mesh::Vertex::Component::Tangent4), "Test.Flat declares no tangent4");
  }

  Report missing;
  check(!findPipelineMaterial(pipeline, "Nope", missing).has_value(), "an unknown material name fails");
  check(missing.has("pipeline.material-not-found"), "an unknown material name reports pipeline.material-not-found");
}

void testGameMeshSpecificationContract() {
  check(gameMeshSpecification(false, false).getVertexStrideInBytes() == LegacyPbrVertexStride,
        "legacy game layout is 36 bytes");
  check(gameMeshSpecification(true, true).getVertexStrideInBytes() == PbrVertexStride, "PBR game layout is 52 bytes");
  check(gameMeshSpecification(true, true).verticesIndexed(), "indexed flag is honoured");
}

void testChannelSynthesis() {
  const mpp::PbrPipelineDocument pipeline = loadPipeline();
  Report setup;
  const TargetMaterial target = *findPipelineMaterial(pipeline, "Test.Indexed", setup);

  Report report;
  ImportOptions options;
  options.joinIdenticalVertices = true;
  std::optional<ModelData> model = importGltf(fixture("triangle-plain.gltf"), options, report);
  check(model.has_value(), "triangle-plain imports");
  if (!model) return;

  check(validateAgainstTarget(*model, target, report), "triangle-plain validates against the 52-byte target");
  check(report.has("channel.synthesised.colour4"), "absent COLOR_0 is reported as synthesised");
  check(report.has("channel.synthesised.tangent4"), "absent TANGENT is reported as synthesised");
  check(!report.has("channel.synthesised.normal3"), "present normals are not reported as synthesised");
  check(!report.hasErrors(), "synthesising derivable channels is not an error");

  // The generated frame must be orthonormal and orthogonal to the normal, or normal mapping is
  // silently wrong rather than obviously broken.
  const Vertex& vertex = model->meshes.front().vertices.front();
  const float dot = vertex.normal[0] * vertex.tangent[0] + vertex.normal[1] * vertex.tangent[1] +
                    vertex.normal[2] * vertex.tangent[2];
  check(std::fabs(dot) < 1e-4f, "generated tangent is orthogonal to the normal");
  const float length = std::sqrt(vertex.tangent[0] * vertex.tangent[0] + vertex.tangent[1] * vertex.tangent[1] +
                                 vertex.tangent[2] * vertex.tangent[2]);
  check(std::fabs(length - 1.0f) < 1e-4f, "generated tangent is unit length");
  check(vertex.tangent[3] == 1.0f || vertex.tangent[3] == -1.0f, "generated tangent handedness is +/-1");
}

void testStrictPromotesWarnings() {
  const mpp::PbrPipelineDocument pipeline = loadPipeline();
  Report setup;
  const TargetMaterial target = *findPipelineMaterial(pipeline, "Test.Indexed", setup);

  Report report;
  report.setStrict(true);
  ImportOptions options;
  options.joinIdenticalVertices = true;
  std::optional<ModelData> model = importGltf(fixture("triangle-plain.gltf"), options, report);
  check(model.has_value(), "triangle-plain imports under strict");
  if (!model) return;

  check(!validateAgainstTarget(*model, target, report), "strict mode refuses a model needing synthesis");
  check(report.hasErrors(), "strict mode records synthesis as an error");
}

void testTangentUnderivableWithoutUvs() {
  const mpp::PbrPipelineDocument pipeline = loadPipeline();
  Report setup;
  const TargetMaterial withTangents = *findPipelineMaterial(pipeline, "Test.Indexed", setup);
  const TargetMaterial withoutTangents = *findPipelineMaterial(pipeline, "Test.Flat", setup);

  Report report;
  std::optional<ModelData> model = importGltf(fixture("triangle-no-uv.gltf"), {}, report);
  check(model.has_value(), "triangle-no-uv imports");
  if (!model) return;

  ModelData forTangentTarget = *model;
  check(!validateAgainstTarget(forTangentTarget, withTangents, report),
        "a UV-less mesh cannot satisfy a tangent4 target");
  check(report.has("channel.underivable.tangent4"), "the failure is reported as underivable, not synthesised");

  // The same mesh is fine against a layout that never asks for tangents.
  Report flatReport;
  ModelData forFlatTarget = *model;
  check(validateAgainstTarget(forFlatTarget, withoutTangents, flatReport),
        "the same UV-less mesh validates against a layout with no tangent4");
}

void testEmbeddedTextureRejected() {
  Report report;
  const std::optional<ModelData> model = importGltf(fixture("textured-embedded.gltf"), {}, report);
  check(!model.has_value(), "an image inlined as a data: URI is refused");
  // AssImp resolves a data: URI image into a container-embedded texture and reports it as "*0",
  // so this fixture lands on the embedded-in-container branch. The data-uri branch remains as
  // defence for importers that pass the URI through verbatim.
  check(report.has("texture.embedded-in-container") || report.has("texture.data-uri"),
        "the refusal names an unreferenceable embedded image");
}

void testExternalTextureAccepted() {
  Report report;
  const std::optional<ModelData> model = importGltf(fixture("textured-external.gltf"), {}, report);
  check(model.has_value(), "an external texture is accepted");
  if (!model) return;
  check(model->materials.front().textures.size() == 1, "the base-colour texture is captured");
  if (!model->materials.front().textures.empty())
    check(model->materials.front().textures.front().sampler == "PBR_BASE_COLOUR_MAP",
          "the base-colour texture binds to PBR_BASE_COLOUR_MAP");
}

void testUnsupportedMaterialFeature() {
  Report report;
  const std::optional<ModelData> model = importGltf(fixture("material-transmission.gltf"), {}, report);
  check(!model.has_value(), "KHR_materials_transmission is refused");
  check(report.has("material.unsupported-feature"), "the refusal is reported as an unsupported feature");
}

void testVertexColoursPreserved() {
  Report report;
  const std::optional<ModelData> model = importGltf(fixture("triangle-coloured.gltf"), {}, report);
  check(model.has_value(), "triangle-coloured imports");
  if (!model) return;
  check(model->meshes.front().source.colours, "COLOR_0 is recognised as present");
  const Vertex& first = model->meshes.front().vertices.front();
  check(first.colour[0] == 255 && first.colour[1] == 0 && first.colour[2] == 0, "the first vertex colour survives");
}

void testNodeHierarchyIsWalked() {
  Report report;
  const std::optional<ModelData> model = importGltf(fixture("hierarchy.gltf"), {}, report);
  check(model.has_value(), "hierarchy imports");
  if (!model) return;

  // One shared mesh instanced under Left, Right and Left's child -- three output meshes, not one.
  check(model->meshes.size() == 3, "each node x mesh instance becomes its own output mesh");

  std::set<std::string> names;
  for (const MeshData& mesh : model->meshes) names.insert(mesh.name);
  check(names.size() == model->meshes.size(), "instance names are unique");
  check(names.count("Left") == 1 && names.count("Right") == 1 && names.count("Child") == 1,
        "output meshes are named from their nodes");

  // Transforms must be baked: Left sits at x=-10, Right at x=+10 with a 2x scale, and Child
  // inherits Left's translation on top of its own.
  for (const MeshData& mesh : model->meshes) {
    const float x = mesh.vertices.front().position[0];
    const float y = mesh.vertices.front().position[1];
    if (mesh.name == "Left") check(std::fabs(x + 10.0f) < 1e-4f, "Left's translation is baked in");
    if (mesh.name == "Right") check(std::fabs(x - 10.0f) < 1e-4f, "Right's translation is baked in");
    if (mesh.name == "Child") {
      check(std::fabs(x + 10.0f) < 1e-4f, "Child inherits its parent's translation");
      check(std::fabs(y - 5.0f) < 1e-4f, "Child's own translation is applied too");
    }
  }
}

void testIndexedRoundTrip() {
  const mpp::PbrPipelineDocument pipeline = loadPipeline();
  const std::filesystem::path out = scratch() / "indexed.mppmodel";
  std::filesystem::remove(out);

  ConvertOptions options;
  options.input = fixture("triangle-plain.gltf");
  options.output = out;
  options.materialName = "Test.Indexed";

  Report report;
  check(convertGltf(pipeline, options, report), "indexed conversion succeeds");
  if (report.hasErrors()) std::cerr << report.format() << '\n';
  check(std::filesystem::exists(out), "the .mppmodel is written");
  if (!std::filesystem::exists(out)) return;

  const ReadModel read = readMppModel(out, true);
  check(read.meshes.size() == 1, "one mesh round-trips");
  check(read.materialNames.size() == 1, "one embedded material round-trips");
  if (read.meshes.empty()) return;

  const ReadMesh& mesh = read.meshes.front();
  check(mesh.vertexStride == PbrVertexStride, "the written stride is the target's 52 bytes");
  check(mesh.vertexCount == 3, "an indexed target keeps the source's three shared vertices");
  check(mesh.primitiveCount == 1, "one triangle");
  check(mesh.indices.size() == 3, "the index stream round-trips");
  check(mesh.name == "PlainNode", "the mesh keeps its node name");
  if (!read.materialNames.empty())
    check(mesh.material == read.materialNames.front(), "the mesh references the embedded material by name");

  // Position of the second vertex is (1,0,0) at offset 0 of stride 52.
  check(std::fabs(readFloat(mesh.vertexBytes, PbrVertexStride + 0) - 1.0f) < 1e-5f, "positions survive packing");
}

void testNonIndexedRoundTrip() {
  const mpp::PbrPipelineDocument pipeline = loadPipeline();
  const std::filesystem::path out = scratch() / "flat.mppmodel";
  std::filesystem::remove(out);

  ConvertOptions options;
  options.input = fixture("triangle-plain.gltf");
  options.output = out;
  options.materialName = "Test.Flat";

  Report report;
  check(convertGltf(pipeline, options, report), "non-indexed conversion succeeds");
  if (report.hasErrors()) std::cerr << report.format() << '\n';
  if (!std::filesystem::exists(out)) return;

  const ReadModel read = readMppModel(out, false);
  check(read.meshes.size() == 1, "one mesh round-trips");
  if (read.meshes.empty()) return;

  const ReadMesh& mesh = read.meshes.front();
  check(mesh.vertexStride == LegacyPbrVertexStride, "the written stride is the target's 36 bytes");
  // The source triangle is indexed 0,1,2; a non-indexed target expands it to a flat stream.
  check(mesh.vertexCount == 3, "the index stream is expanded into a flat vertex stream");
  check(mesh.primitiveCount == 1, "one triangle");
}

// A texture must be inside the output model's directory to be referenced relatively, so the
// fixture and its image are copied into the scratch directory and converted there.
void testTextureRoundTripPreservesColourSpace() {
  const mpp::PbrPipelineDocument pipeline = loadPipeline();
  const std::filesystem::path directory = scratch() / "textured";
  std::filesystem::create_directories(directory);
  std::filesystem::copy_file(fixture("textured-external.gltf"), directory / "textured-external.gltf",
                             std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(fixture("fixture-texture.png"), directory / "fixture-texture.png",
                             std::filesystem::copy_options::overwrite_existing);

  const std::filesystem::path out = directory / "textured.mppmodel";
  std::filesystem::remove(out);

  ConvertOptions options;
  options.input = directory / "textured-external.gltf";
  options.output = out;
  options.materialName = "Test.Indexed";

  Report report;
  check(convertGltf(pipeline, options, report), "a textured model converts");
  if (report.hasErrors()) std::cerr << report.format() << '\n';
  if (!std::filesystem::exists(out)) return;

  const ReadModel read = readMppModel(out, true);
  check(read.materials.size() == 1, "one embedded material round-trips");
  if (read.materials.empty()) return;

  auto* material = dynamic_cast<mpp::PbrMaterialStream*>(read.materials.front().get());
  check(material != nullptr, "the embedded material deserializes as a PbrMaterial");
  if (material == nullptr) return;

  const auto& textures = material->getTextures();
  check(textures.size() == 1, "the base-colour texture binding round-trips");
  if (textures.empty()) return;
  check(textures.front().sampler == "PBR_BASE_COLOUR_MAP", "the sampler name round-trips");
  check(textures.front().isChild, "the texture is recorded as a child stream");
  check(textures.front().source == "fixture-texture.png", "the texture path is relative to the model");
  // The point of the RSE4 stream-format change: before it, this came back Linear and the import
  // shaded incorrectly.
  check(textures.front().params.colourSpace == mpp::TextureColourSpace::Srgb,
        "an sRGB base-colour map survives serialization as sRGB");

  // The child TextureStream carries the same colour space, and is what MppModelStream rebases.
  const auto& children = read.materials.front()->getChildren();
  const auto child = children.find("Textures/PBR_BASE_COLOUR_MAP");
  check(child != children.end(), "the child TextureStream round-trips under its sampler key");
  if (child == children.end()) return;
  auto* textureStream = dynamic_cast<mpp::TextureStream*>(child->second.get());
  check(textureStream != nullptr, "the child deserializes as a TextureStream");
  if (textureStream != nullptr)
    check(textureStream->getParams().colourSpace == mpp::TextureColourSpace::Srgb,
          "the child TextureStream's colour space survives too");
}

// legacy-rse3.mppmodel is a frozen copy of a model written before the RSE4 stream-format change
// (MassivePolyPusher's own demo-suite cube: one indexed mesh with one embedded BasicMaterial). It
// is committed rather than read out of the submodule so the compatibility path keeps being
// exercised even if every asset over there is re-exported. BasicMaterial shares the same
// texture-options record that RSE4 extended, so this covers the read path most at risk of
// desynchronising.
void testLegacyRse3StreamStillReads() {
  const ReadModel read = readMppModel(fixture("legacy-rse3.mppmodel"), true);
  check(read.meshes.size() == 1, "a pre-RSE4 model still reads its mesh");
  check(read.materials.size() == 1, "a pre-RSE4 embedded material still deserializes");
  if (read.materials.empty()) return;

  auto* basic = dynamic_cast<mpp::BasicMaterialStream*>(read.materials.front().get());
  check(basic != nullptr, "the legacy embedded material deserializes as a BasicMaterial");
  if (basic == nullptr) return;

  // RSE3 carried no colour-space field, so every texture legitimately comes back Linear. What is
  // actually under test is that the reader does not consume a field that isn't there and
  // misparse everything after it.
  for (const auto& texture : basic->getTextures())
    check(texture.params.colourSpace == mpp::TextureColourSpace::Linear,
          "a pre-RSE4 texture defaults to linear rather than reading garbage");

  // A desynchronised read would corrupt the MeshSpecification that precedes the texture list, so
  // a sane stride is strong evidence the whole record stayed aligned.
  const std::size_t stride = basic->getMeshSpecification().getVertexStrideInBytes();
  check(stride > 0 && stride <= 256, "the legacy material's MeshSpecification survives intact");
}

void testValidateOnlyWritesNothing() {
  const mpp::PbrPipelineDocument pipeline = loadPipeline();
  const std::filesystem::path out = scratch() / "should-not-exist.mppmodel";
  std::filesystem::remove(out);

  ConvertOptions options;
  options.input = fixture("triangle-plain.gltf");
  options.output = out;
  options.materialName = "Test.Indexed";
  options.validateOnly = true;

  Report report;
  check(convertGltf(pipeline, options, report), "validate-only succeeds");
  check(!std::filesystem::exists(out), "validate-only writes no file");
}

}  // namespace

int main() {
  try {
    testPipelineMaterialLookup();
    testGameMeshSpecificationContract();
    testChannelSynthesis();
    testStrictPromotesWarnings();
    testTangentUnderivableWithoutUvs();
    testEmbeddedTextureRejected();
    testExternalTextureAccepted();
    testUnsupportedMaterialFeature();
    testVertexColoursPreserved();
    testNodeHierarchyIsWalked();
    testIndexedRoundTrip();
    testNonIndexedRoundTrip();
    testTextureRoundTripPreservesColourSpace();
    testLegacyRse3StreamStillReads();
    testValidateOnlyWritesNothing();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
    ++failures;
  }

  if (failures == 0) std::cout << "model_io_tests: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
