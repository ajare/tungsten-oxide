// GltfConvert.hpp — the whole glTF -> .mppmodel operation, and the validation pass it rests on.
//
// This is the entry point both consumers call: gltf_convert directly, and the editor's import
// dialog via validateAgainstTarget() first (to show the report) and convertGltf() on accept.
#pragma once

#include <filesystem>
#include <string>

#include <mpp/PbrPipelineDocument.h>

#include "modelio/Diagnostics.hpp"
#include "modelio/ModelData.hpp"
#include "modelio/PipelineMaterial.hpp"

namespace modelio {

// Brings `model` up to the target's MeshSpecification, synthesising every channel it names that
// the source did not supply and reporting each substitution (docs/adr/0004-gltf-import.md, D7),
// then checks each material's required features against what mpp can express and what the layout
// can feed (D3). Mutates `model`. Returns false if the report gained an error.
bool validateAgainstTarget(ModelData& model, const TargetMaterial& target, Report& report);

struct ConvertOptions {
  std::filesystem::path input;
  std::filesystem::path output;  // ignored when validateOnly
  std::string materialName;
  bool strict{false};
  bool validateOnly{false};
};

// Returns false having reported. On failure no output file is left behind.
bool convertGltf(const mpp::PbrPipelineDocument& pipeline, const ConvertOptions& options, Report& report);

}  // namespace modelio
