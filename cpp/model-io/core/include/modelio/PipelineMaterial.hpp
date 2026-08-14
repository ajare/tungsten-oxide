// PipelineMaterial.hpp — resolving "the material the caller named" out of a PBR pipeline document.
//
// A PbrPipelineDocument holds each local resource as a raw mpp::data::StructuredData payload whose root
// matches its kind, so the MeshSpecification lives in the document exactly as authored and is
// parsed with mpp's own MeshSpecificationParser -- the same parser FilePbrMaterialStream uses, so a
// spec accepted here is a spec the runtime will accept.
//
// A pipeline's materials do NOT share one layout (in TungstenMonoxide.pipeline.xml Ship.Surface is
// indexed and the seven Track.* are not), which is why the caller names one rather than the
// document implying it -- see docs/adr/0004-gltf-import.md, D3.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mpp/PbrPipelineDocument.h>
#include <mpp/mesh/MeshSpecification.h>

#include "modelio/Diagnostics.hpp"

namespace modelio {

struct TargetMaterial {
  std::string name;
  mpp::mesh::MeshSpecification meshSpec;
  // Set when the pipeline material names an external program by <Ref>; empty means the material
  // uses mpp's built-in PBR shaders specialised from its own derived features, which is what every
  // material in TungstenMonoxide.pipeline.xml does.
  std::string programRef;
};

// Every PbrMaterial resource in the document, local first then external, in declaration order --
// what the editor's import dialog offers and what the console tool lists on an unknown name.
std::vector<std::string> pipelineMaterialNames(const mpp::PbrPipelineDocument& document);

// Reports and returns nullopt when the name matches no PbrMaterial, when the material declares no
// MeshSpecification, or when that specification fails to parse.
std::optional<TargetMaterial> findPipelineMaterial(const mpp::PbrPipelineDocument& document,
                                                   const std::string& materialName, Report& report);

}  // namespace modelio
