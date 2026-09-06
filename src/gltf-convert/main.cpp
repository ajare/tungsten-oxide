// gltf_convert — glTF/GLB -> .mppmodel, validated against a PBR pipeline specification.
//
// The headless counterpart to the editor's "Import glTF..." action, and this repository's analogue
// of MassivePolyPusher's own ModelConvert. Import only: glTF export is explicitly out of scope
// (docs/GLTF_IMPORT_PLAN.md).
//
//   gltf_convert --in model.gltf --pipeline X.pipeline.yaml --material Ship.Surface --out model.mppmodel
//   gltf_convert --pipeline X.pipeline.yaml --list-materials
//   gltf_convert --in model.gltf --pipeline X.pipeline.yaml --material Ship.Surface --validate-only
//
// Exit status is 0 only when the conversion (or validation) succeeded with no errors.
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <mpp/resource-parsers/PbrPipelineDocumentLoader.h>

#include "ExecutableRuntime.hpp"
#include "modelio/Diagnostics.hpp"
#include "modelio/GltfConvert.hpp"
#include "modelio/PipelineMaterial.hpp"

namespace {

void printUsage() {
  std::cerr << "usage: gltf_convert --in <model.gltf> --pipeline <pipeline.xml> --material <name>\n"
               "                    [--out <model.mppmodel>] [--strict] [--validate-only]\n"
               "       gltf_convert --pipeline <pipeline.xml> --list-materials\n"
               "\n"
               "  --in             source glTF or GLB\n"
               "  --pipeline       PBR pipeline XML supplying the target MeshSpecification\n"
               "  --material       which PbrMaterial in that pipeline to target\n"
               "  --out            destination .mppmodel (required unless --validate-only)\n"
               "  --strict         treat every warning (synthesised channels) as an error\n"
               "  --validate-only  run both validations and report, writing nothing\n"
               "  --list-materials print the pipeline's PbrMaterial names and exit\n";
}

struct Arguments {
  std::filesystem::path input;
  std::filesystem::path output;
  std::filesystem::path pipeline;
  std::string material;
  bool strict{false};
  bool validateOnly{false};
  bool listMaterials{false};
};

// Returns false on a malformed command line, having already explained why.
bool parseArguments(int argc, char** argv, Arguments& out) {
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    const bool needsValue = flag == "--in" || flag == "--out" || flag == "--pipeline" || flag == "--material";
    if (needsValue && i + 1 >= argc) {
      tox::runtime::reportError("gltf_convert: " + flag + " requires a value", 2);
      return false;
    }

    if (flag == "--in")
      out.input = argv[++i];
    else if (flag == "--out")
      out.output = argv[++i];
    else if (flag == "--pipeline")
      out.pipeline = argv[++i];
    else if (flag == "--material")
      out.material = argv[++i];
    else if (flag == "--strict")
      out.strict = true;
    else if (flag == "--validate-only")
      out.validateOnly = true;
    else if (flag == "--list-materials")
      out.listMaterials = true;
    else if (flag == "--help" || flag == "-h") {
      tox::runtime::reportError("gltf_convert: --help must be used alone", 2);
      return false;
    } else {
      tox::runtime::reportError("gltf_convert: unrecognised argument '" + flag + "'", 2);
      return false;
    }
  }

  if (out.pipeline.empty()) {
    tox::runtime::reportError("gltf_convert: --pipeline is required", 2);
    return false;
  }
  if (out.listMaterials) return true;

  if (out.input.empty()) {
    tox::runtime::reportError("gltf_convert: --in is required", 2);
    return false;
  }
  if (out.material.empty()) {
    tox::runtime::reportError("gltf_convert: --material is required", 2);
    return false;
  }
  if (out.output.empty() && !out.validateOnly) {
    tox::runtime::reportError("gltf_convert: --out is required unless --validate-only is given", 2);
    return false;
  }
  return true;
}

}  // namespace

int run(int argc, char** argv) {
  if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
    printUsage();
    return 0;
  }

  Arguments arguments;
  if (!parseArguments(argc, argv, arguments)) {
    printUsage();
    return 2;
  }

  mpp::PbrPipelineDocument pipeline;
  try {
    pipeline = mpp::resource_parsers::PbrPipelineDocumentLoader::fromFile(arguments.pipeline.string());
  } catch (const std::exception& error) {
    return tox::runtime::reportError("gltf_convert: could not load pipeline '" + arguments.pipeline.string() +
                                     "': " + error.what());
  }

  if (arguments.listMaterials) {
    for (const std::string& name : modelio::pipelineMaterialNames(pipeline)) std::cout << name << "\n";
    return 0;
  }

  modelio::ConvertOptions options;
  options.input = arguments.input;
  options.output = arguments.output;
  options.materialName = arguments.material;
  options.strict = arguments.strict;
  options.validateOnly = arguments.validateOnly;

  modelio::Report report;
  bool succeeded = false;
  try {
    succeeded = modelio::convertGltf(pipeline, options, report);
  } catch (const std::exception& error) {
    return tox::runtime::reportError(std::string("gltf_convert: ") + error.what());
  }

  const std::string formatted = report.format();
  if (!formatted.empty()) std::cerr << formatted << "\n";

  if (!succeeded)
    return tox::runtime::reportError("gltf_convert: failed (" + std::to_string(report.errorCount()) + " error(s), " +
                                     std::to_string(report.warningCount()) + " warning(s))");

  if (arguments.validateOnly)
    std::cout << "gltf_convert: validation passed (" << report.warningCount() << " warning(s))\n";
  else
    std::cout << "gltf_convert: wrote " << arguments.output.string() << " (" << report.warningCount()
              << " warning(s))\n";
  return 0;
}

int main(int argc, char** argv) {
  return tox::runtime::guardedMain([&] { return run(argc, argv); });
}
