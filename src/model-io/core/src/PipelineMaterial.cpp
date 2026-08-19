#include "modelio/PipelineMaterial.hpp"

#include <exception>

#include <mpp/resource-parsers/MeshSpecificationParser.h>

namespace modelio {
namespace {

const mpp::PbrPipelineResourceDocument* findMaterialResource(const mpp::PbrPipelineDocument& document,
                                                             const std::string& name) {
  for (const auto& resource : document.localResources)
    if (resource.kind == mpp::PbrPipelineResourceKind::PbrMaterial && resource.name == name) return &resource;
  for (const auto& external : document.externalResources)
    if (external.resource.kind == mpp::PbrPipelineResourceKind::PbrMaterial && external.resource.name == name)
      return &external.resource;
  return nullptr;
}

std::string programRefOf(const mpp::data::StructuredData& definition) {
  if (!definition.hasEntry("Program")) return {};
  const auto& program = definition.getEntry("Program");
  return program.hasEntry("Ref") ? program.getEntry("Ref").getValue() : std::string{};
}

}  // namespace

std::vector<std::string> pipelineMaterialNames(const mpp::PbrPipelineDocument& document) {
  std::vector<std::string> names;
  for (const auto& resource : document.localResources)
    if (resource.kind == mpp::PbrPipelineResourceKind::PbrMaterial) names.push_back(resource.name);
  for (const auto& external : document.externalResources)
    if (external.resource.kind == mpp::PbrPipelineResourceKind::PbrMaterial) names.push_back(external.resource.name);
  return names;
}

std::optional<TargetMaterial> findPipelineMaterial(const mpp::PbrPipelineDocument& document,
                                                   const std::string& materialName, Report& report) {
  const mpp::PbrPipelineResourceDocument* resource = findMaterialResource(document, materialName);
  if (resource == nullptr) {
    std::string available;
    for (const std::string& name : pipelineMaterialNames(document)) {
      if (!available.empty()) available += ", ";
      available += name;
    }
    report.error("pipeline.material-not-found",
                 "pipeline '" + document.name + "' declares no PbrMaterial named '" + materialName +
                     "'; available: " + (available.empty() ? "(none)" : available));
    return std::nullopt;
  }

  if (!resource->definition.hasEntry("MeshSpecification")) {
    report.error("pipeline.material-no-mesh-specification",
                 "material declares no <MeshSpecification>, so there is no target vertex layout to convert to", {},
                 materialName);
    return std::nullopt;
  }

  TargetMaterial target;
  target.name = materialName;
  target.programRef = programRefOf(resource->definition);
  try {
    mpp::resource_parsers::MeshSpecificationParser parser(document.sourcePath);
    target.meshSpec = parser.parse(resource->definition.getEntry("MeshSpecification"));
  } catch (const std::exception& error) {
    report.error("pipeline.mesh-specification-invalid",
                 std::string("could not parse the material's <MeshSpecification>: ") + error.what(), {}, materialName);
    return std::nullopt;
  }

  if (target.meshSpec.getPrimitiveType() != mpp::mesh::Primitive::Type::Triangles) {
    report.error("pipeline.non-triangle-primitive",
                 "the material's MeshSpecification asks for a non-triangle primitive; glTF import only produces "
                 "triangle lists",
                 {}, materialName);
    return std::nullopt;
  }

  return target;
}

}  // namespace modelio
