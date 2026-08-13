#include "modelio/GltfConvert.hpp"

#include <set>
#include <string>

#include "modelio/AssetImport.hpp"
#include "modelio/MeshLayout.hpp"
#include "modelio/MppModelIo.hpp"
#include "modelio/Tangents.hpp"

namespace modelio {
namespace {

using Component = mpp::mesh::Vertex::Component;

bool validateMeshChannels(MeshData& mesh, const TargetMaterial& target, Report& report) {
  const auto& spec = target.meshSpec;

  if (specHasComponent(spec, Component::Normal3) && !mesh.source.normals) {
    generateSmoothNormals(mesh);
    report.warn("channel.synthesised.normal3",
                "source has no normals; area-weighted smooth normals were generated", mesh.name);
  }

  const bool wantsUv = specHasComponent(spec, Component::TexCoord2);
  if (wantsUv && !mesh.source.uvs)
    report.warn("channel.synthesised.texcoord2", "source has no UV set; texture coordinates default to (0,0)",
                mesh.name);

  if ((specHasComponent(spec, Component::Colour4) || specHasComponent(spec, Component::Colour3)) &&
      !mesh.source.colours)
    report.warn("channel.synthesised.colour4", "source has no vertex colours; they default to opaque white",
                mesh.name);

  if (specHasComponent(spec, Component::Tangent4) && !mesh.source.tangents) {
    if (!mesh.source.uvs) {
      report.error("channel.underivable.tangent4",
                   "the target layout requires tangent4, but the source has neither tangents nor the UVs needed "
                   "to derive them",
                   mesh.name);
      return false;
    }
    generateTangents(mesh);
    report.warn("channel.synthesised.tangent4", "source has no tangents; they were derived from UVs and normals",
                mesh.name);
  }

  return true;
}

bool validateMaterialFeatures(const MaterialData& material, const TargetMaterial& target, Report& report) {
  bool ok = true;

  for (const TextureRef& texture : material.textures) {
    // The one genuinely layout-dependent material constraint: normal mapping needs a tangent frame
    // in the vertex stream, and no amount of shader specialisation substitutes for it.
    if (texture.sampler == "PBR_NORMAL_MAP" && !specHasComponent(target.meshSpec, Component::Tangent4)) {
      report.error("material.normal-map-without-tangents",
                   "material uses a normal map, but the target material's MeshSpecification declares no tangent4 "
                   "channel to feed it",
                   {}, material.name);
      ok = false;
    }
  }

  if (material.alphaMode == AlphaMode::Blend)
    report.warn("material.alpha-blend",
                "material is alpha-blended; the pipeline must provide a pass that blends, which is not checked here",
                {}, material.name);

  return ok;
}

}  // namespace

bool validateAgainstTarget(ModelData& model, const TargetMaterial& target, Report& report) {
  bool ok = true;

  for (MeshData& mesh : model.meshes)
    if (!validateMeshChannels(mesh, target, report)) ok = false;

  // Only materials some mesh actually uses: an unreferenced glTF material should not be able to
  // fail a conversion whose output would never touch it.
  std::set<int> used;
  for (const MeshData& mesh : model.meshes) used.insert(mesh.materialIndex);
  for (const int index : used)
    if (!validateMaterialFeatures(model.materials[static_cast<std::size_t>(index)], target, report)) ok = false;

  return ok && !report.hasErrors();
}

bool convertGltf(const mpp::PbrPipelineDocument& pipeline, const ConvertOptions& options, Report& report) {
  report.setStrict(options.strict);

  const std::optional<TargetMaterial> target = findPipelineMaterial(pipeline, options.materialName, report);
  if (!target) return false;

  ImportOptions importOptions;
  importOptions.joinIdenticalVertices = target->meshSpec.verticesIndexed();

  std::optional<ModelData> model = importAsset(options.input, importOptions, report);
  if (!model) return false;

  if (!validateAgainstTarget(*model, *target, report)) return false;

  if (options.validateOnly) return true;

  return writeMppModel(*model, *target, options.output, report) && !report.hasErrors();
}

}  // namespace modelio
