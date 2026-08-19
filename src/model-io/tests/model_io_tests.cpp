// model_io_tests.cpp — headless tests for src/model-io (docs/GLTF_IMPORT_PLAN.md, M1).
//
// Covers the validation matrix (which missing channels are synthesised, which are fatal, which
// material features are refused) and a full round-trip of the written .mppmodel back through
// mpp::ModelSerializer, against the committed fixtures in src/test-data/fixtures/gltf.
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

#include "modelio/AssetExport.hpp"
#include "modelio/GltfConvert.hpp"
#include "modelio/AssetImport.hpp"
#include "modelio/MeshLayout.hpp"
#include "modelio/MppModelIo.hpp"
#include "modelio/PbrMaterialBuild.hpp"
#include "modelio/PbrMaterialRead.hpp"
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
  return mpp::resource_parsers::PbrPipelineDocumentLoader::fromFile(fixture("test.pipeline.yaml").string());
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
  std::optional<ModelData> model = importAsset(fixture("triangle-plain.gltf"), options, report);
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
  std::optional<ModelData> model = importAsset(fixture("triangle-plain.gltf"), options, report);
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
  std::optional<ModelData> model = importAsset(fixture("triangle-no-uv.gltf"), {}, report);
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
  const std::optional<ModelData> model = importAsset(fixture("textured-embedded.gltf"), {}, report);
  check(!model.has_value(), "an image inlined as a data: URI is refused");
  // AssImp resolves a data: URI image into a container-embedded texture and reports it as "*0",
  // so this fixture lands on the embedded-in-container branch. The data-uri branch remains as
  // defence for importers that pass the URI through verbatim.
  check(report.has("texture.embedded-in-container") || report.has("texture.data-uri"),
        "the refusal names an unreferenceable embedded image");
}

// src/model-tool references materials by name and only wants a path to preview with, so an
// unreferenceable image costs it a placeholder rather than the whole import (ADR 0001 D4).
void testEmbeddedTextureSkippedUnderSkipPolicy() {
  Report report;
  ImportOptions options;
  options.embeddedTextures = EmbeddedTexturePolicy::Skip;

  const std::optional<ModelData> model = importAsset(fixture("textured-embedded.gltf"), options, report);
  check(model.has_value(), "Skip policy imports a model whose image is packed in the container");
  if (!model) return;
  check(!report.hasErrors(), "Skip policy records the dropped binding as a warning, not an error");

  const MaterialData& material = model->materials.front();
  check(material.skippedEmbeddedTexture, "the material is flagged as having lost a texture");
  check(material.textures.empty(), "the unreferenceable binding is dropped rather than kept");
}

// AssImp's glTF2 importer appends a default material to every scene whether or not anything uses
// it; a UI listing materials should not show it.
void testUnreferencedMaterialsPruned() {
  Report unpruned;
  const std::optional<ModelData> withDefault = importAsset(fixture("triangle-plain.gltf"), {}, unpruned);
  check(withDefault.has_value(), "triangle-plain imports without pruning");

  Report report;
  ImportOptions options;
  options.pruneUnreferencedMaterials = true;
  const std::optional<ModelData> pruned = importAsset(fixture("triangle-plain.gltf"), options, report);
  check(pruned.has_value(), "triangle-plain imports with pruning");
  if (!withDefault || !pruned) return;

  check(withDefault->materials.size() == 2, "AssImp contributes an unreferenced default material");
  check(pruned->materials.size() == 1, "pruning drops it");
  for (const MeshData& mesh : pruned->meshes)
    check(mesh.materialIndex >= 0 && static_cast<std::size_t>(mesh.materialIndex) < pruned->materials.size(),
          "every mesh's material index is remapped into range");
}

void testExternalTextureAccepted() {
  Report report;
  const std::optional<ModelData> model = importAsset(fixture("textured-external.gltf"), {}, report);
  check(model.has_value(), "an external texture is accepted");
  if (!model) return;
  check(model->materials.front().textures.size() == 1, "the base-colour texture is captured");
  if (!model->materials.front().textures.empty())
    check(model->materials.front().textures.front().sampler == "PBR_BASE_COLOUR_MAP",
          "the base-colour texture binds to PBR_BASE_COLOUR_MAP");
}

void testUnsupportedMaterialFeature() {
  Report report;
  const std::optional<ModelData> model = importAsset(fixture("material-transmission.gltf"), {}, report);
  check(!model.has_value(), "KHR_materials_transmission is refused");
  check(report.has("material.unsupported-feature"), "the refusal is reported as an unsupported feature");
}

void testVertexColoursPreserved() {
  Report report;
  const std::optional<ModelData> model = importAsset(fixture("triangle-coloured.gltf"), {}, report);
  check(model.has_value(), "triangle-coloured imports");
  if (!model) return;
  check(model->meshes.front().source.colours, "COLOR_0 is recognised as present");
  const Vertex& first = model->meshes.front().vertices.front();
  check(first.colour[0] == 255 && first.colour[1] == 0 && first.colour[2] == 0, "the first vertex colour survives");
}

void testNodeHierarchyIsWalked() {
  Report report;
  const std::optional<ModelData> model = importAsset(fixture("hierarchy.gltf"), {}, report);
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

// docs/GLTF_IMPORT_PLAN.md "Export All..." feature: AssetExport.cpp's ModelData -> aiScene -> AssImp
// Exporter path, round-tripped back through the existing AssetImport.cpp for verification (there is
// no independent glTF parser in this test binary to check against).
void testExportRoundTrip(bool binary) {
  const std::string label = binary ? " (glb)" : " (gltf)";

  ModelData model;
  MaterialData material;
  material.name = "Test/ExportMaterial";
  material.baseColourFactor[0] = 0.2f;
  material.baseColourFactor[1] = 0.4f;
  material.baseColourFactor[2] = 0.6f;
  material.baseColourFactor[3] = 1.0f;
  material.metallicFactor = 0.35f;
  material.roughnessFactor = 0.65f;
  model.materials.push_back(material);

  MeshData mesh;
  mesh.name = "Triangle";
  mesh.materialIndex = 0;
  Vertex v0, v1, v2;
  v0.position[0] = 0.0f;
  v0.position[1] = 0.0f;
  v0.position[2] = 0.0f;
  v1.position[0] = 1.0f;
  v1.position[1] = 0.0f;
  v1.position[2] = 0.0f;
  v2.position[0] = 0.0f;
  v2.position[1] = 1.0f;
  v2.position[2] = 0.0f;
  v0.normal[2] = v1.normal[2] = v2.normal[2] = 1.0f;
  v0.normal[0] = v0.normal[1] = v1.normal[0] = v1.normal[1] = v2.normal[0] = v2.normal[1] = 0.0f;
  mesh.vertices = {v0, v1, v2};
  mesh.indices = {0, 1, 2};
  model.meshes.push_back(mesh);

  const std::filesystem::path out = scratch() / (std::string("export-test") + (binary ? ".glb" : ".gltf"));
  std::filesystem::remove(out);

  ExportOptions exportOptions;
  exportOptions.binary = binary;
  Report exportReport;
  check(exportAsset(model, out, exportOptions, exportReport), "exportAsset succeeds" + label);
  if (exportReport.hasErrors()) std::cerr << exportReport.format() << '\n';
  check(std::filesystem::exists(out), "export writes a file" + label);
  if (!std::filesystem::exists(out)) return;

  ImportOptions importOptions;
  Report importReport;
  const std::optional<ModelData> reimported = importAsset(out, importOptions, importReport);
  check(reimported.has_value(), "the exported file re-imports" + label);
  if (importReport.hasErrors()) std::cerr << importReport.format() << '\n';
  if (!reimported) return;

  check(reimported->meshes.size() == 1, "one mesh survives re-import" + label);
  if (!reimported->meshes.empty())
    check(reimported->meshes.front().vertices.size() == 3, "vertex count survives" + label);
  check(!reimported->materials.empty(), "at least one material survives" + label);
  if (!reimported->materials.empty()) {
    const MaterialData& readMaterial = reimported->materials.front();
    check(std::fabs(readMaterial.metallicFactor - 0.35f) < 1e-4, "metallic factor survives" + label);
    check(std::fabs(readMaterial.roughnessFactor - 0.65f) < 1e-4, "roughness factor survives" + label);
  }
}

// PbrMaterialRead.cpp is the direct inverse of PbrMaterialBuild.cpp: build an embedded material in
// memory, read it straight back (no serialization round trip needed here -- RSE4's own on-disk
// round trip is already covered by testTextureRoundTripPreservesColourSpace above), and check every
// PbrSurface field plus the one texture binding survive.
void testPbrMaterialReadRoundTrip() {
  const mpp::PbrPipelineDocument pipeline = loadPipeline();
  Report targetReport;
  const std::optional<TargetMaterial> target = findPipelineMaterial(pipeline, "Test.Indexed", targetReport);
  check(target.has_value(), "target material resolves for the PbrMaterialRead test");
  if (!target) return;

  MaterialData material;
  material.name = "Test/ReadMaterial";
  material.baseColourFactor[0] = 0.25f;
  material.baseColourFactor[1] = 0.5f;
  material.baseColourFactor[2] = 0.75f;
  material.baseColourFactor[3] = 0.9f;
  material.metallicFactor = 0.3f;
  material.roughnessFactor = 0.6f;
  material.emissiveFactor[0] = 0.1f;
  material.emissiveFactor[1] = 0.2f;
  material.emissiveFactor[2] = 0.3f;
  material.alphaMode = AlphaMode::Mask;
  material.alphaCutoff = 0.4f;
  material.doubleSided = true;
  material.textures.push_back({"PBR_BASE_COLOUR_MAP", (fixtures() / "fixture-texture.png").string()});

  Report buildReport;
  const mpp::ResourceStreamPtr stream = buildEmbeddedPbrMaterial(material, *target, fixtures(), buildReport);
  check(stream != nullptr, "buildEmbeddedPbrMaterial succeeds for the read-back test");
  if (!stream) return;

  MaterialData readBack;
  check(readEmbeddedPbrMaterial(stream, fixtures(), readBack), "readEmbeddedPbrMaterial recognises a PbrMaterialStream");
  check(std::fabs(readBack.baseColourFactor[0] - 0.25f) < 1e-4, "base colour r round-trips");
  check(std::fabs(readBack.baseColourFactor[3] - 0.9f) < 1e-4, "base colour a round-trips");
  check(std::fabs(readBack.metallicFactor - 0.3f) < 1e-4, "metallic factor round-trips");
  check(std::fabs(readBack.roughnessFactor - 0.6f) < 1e-4, "roughness factor round-trips");
  check(std::fabs(readBack.emissiveFactor[1] - 0.2f) < 1e-4, "emissive factor round-trips");
  check(readBack.alphaMode == AlphaMode::Mask, "alpha mode round-trips");
  check(std::fabs(readBack.alphaCutoff - 0.4f) < 1e-4, "alpha cutoff round-trips");
  check(readBack.doubleSided, "double-sided round-trips");

  check(readBack.textures.size() == 1, "one texture round-trips");
  if (!readBack.textures.empty()) {
    check(readBack.textures.front().sampler == "PBR_BASE_COLOUR_MAP", "texture sampler round-trips");
    const std::string expectedPath = (fixtures() / "fixture-texture.png").lexically_normal().string();
    check(readBack.textures.front().path == expectedPath, "texture path round-trips to an absolute path");
  }
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
    testEmbeddedTextureSkippedUnderSkipPolicy();
    testUnreferencedMaterialsPruned();
    testExternalTextureAccepted();
    testUnsupportedMaterialFeature();
    testVertexColoursPreserved();
    testNodeHierarchyIsWalked();
    testIndexedRoundTrip();
    testNonIndexedRoundTrip();
    testTextureRoundTripPreservesColourSpace();
    testLegacyRse3StreamStillReads();
    testValidateOnlyWritesNothing();
    testExportRoundTrip(/*binary=*/false);
    testExportRoundTrip(/*binary=*/true);
    testPbrMaterialReadRoundTrip();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
    ++failures;
  }

  if (failures == 0) std::cout << "model_io_tests: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
