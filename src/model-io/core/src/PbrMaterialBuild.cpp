#include "modelio/PbrMaterialBuild.hpp"

#include <system_error>

#include <GL/glew.h>

#include <mpp/PbrMaterialSpecification.h>
#include <mpp/ProgrammaticPbrMaterialStream.h>
#include <mpp/ProgrammaticTextureStream.h>

namespace modelio {
namespace {

using Spec = mpp::PbrMaterialSpecification;

// The child-resource key convention FilePbrMaterialStream uses for a pipeline-authored map.
std::string childKey(const std::string& sampler) { return "Textures/" + sampler; }

Spec::PbrAlphaMode alphaModeOf(AlphaMode mode) {
  switch (mode) {
    case AlphaMode::Mask: return Spec::PbrAlphaMode::Mask;
    case AlphaMode::Blend: return Spec::PbrAlphaMode::Blend;
    case AlphaMode::Opaque: break;
  }
  return Spec::PbrAlphaMode::Opaque;
}

// Trilinear + repeat + mipmaps, matching every <Resource> block in TungstenMonoxide.pipeline.yaml.
// TextureParams' own defaults are GL_NEAREST with no mipmaps, which would look nothing like the
// rest of the game, so these are set explicitly rather than left alone.
mpp::TextureParams defaultTextureParams(bool srgb) {
  mpp::TextureParams params;
  params.minFilter = GL_LINEAR_MIPMAP_LINEAR;
  params.magFilter = GL_LINEAR;
  params.wrap = GL_REPEAT;
  params.useMipmaps = true;
  params.colourSpace = srgb ? mpp::TextureColourSpace::Srgb : mpp::TextureColourSpace::Linear;
  return params;
}

// Relative, forward-slashed, and provably inside the model's directory tree.
bool relativeTexturePath(const std::filesystem::path& texture, const std::filesystem::path& modelDirectory,
                         const MaterialData& material, const TextureRef& reference, Report& report,
                         std::string& out) {
  std::error_code error;
  const std::filesystem::path canonicalTexture = std::filesystem::weakly_canonical(texture, error);
  const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(modelDirectory, error);
  const std::filesystem::path relative =
      std::filesystem::relative(error ? texture : canonicalTexture, error ? modelDirectory : canonicalRoot, error);

  if (error || relative.empty() || *relative.begin() == "..") {
    report.error("texture.outside-model-directory",
                 "texture '" + reference.path + "' for sampler " + reference.sampler +
                     " is not inside the output model's directory ('" + modelDirectory.string() +
                     "'), so it cannot be referenced by a relative path",
                 {}, material.name);
    return false;
  }

  out = relative.generic_string();
  return true;
}

}  // namespace

bool samplerIsSrgb(const std::string& sampler) {
  return sampler == "PBR_BASE_COLOUR_MAP" || sampler == "PBR_EMISSIVE_MAP";
}

mpp::ResourceStreamPtr buildEmbeddedPbrMaterial(const MaterialData& material, const TargetMaterial& target,
                                                const std::filesystem::path& modelDirectory, Report& report) {
  Spec specification;

  specification.program.spec = target.meshSpec;
  specification.program.is2d = false;
  if (!target.programRef.empty()) {
    specification.program.resourceExists = true;
    specification.program.isChild = false;
    specification.program.existingResource = target.programRef;
  }
  // Otherwise resourceExists stays false, and PbrMaterial builds a default 3D program from the
  // built-in PBR shaders, this MeshSpecification and the features derived below -- which is what
  // every material in TungstenMonoxide.pipeline.yaml already does.

  specification.pbr.enabled = true;
  specification.pbr.baseColourFactor = {material.baseColourFactor[0], material.baseColourFactor[1],
                                        material.baseColourFactor[2], material.baseColourFactor[3]};
  specification.pbr.metallicFactor = material.metallicFactor;
  specification.pbr.roughnessFactor = material.roughnessFactor;
  specification.pbr.emissiveFactor = {material.emissiveFactor[0], material.emissiveFactor[1],
                                      material.emissiveFactor[2]};
  specification.pbr.normalScale = material.normalScale;
  specification.pbr.occlusionStrength = material.occlusionStrength;
  specification.pbr.alphaMode = alphaModeOf(material.alphaMode);
  specification.pbr.alphaCutoff = material.alphaCutoff;
  specification.pbr.doubleSided = material.doubleSided;

  struct PendingTexture {
    std::string key;
    std::string sampler;
    std::string relativePath;
    bool srgb{false};
  };
  std::vector<PendingTexture> pending;

  for (const TextureRef& reference : material.textures) {
    std::string relative;
    if (!relativeTexturePath(reference.path, modelDirectory, material, reference, report, relative)) return nullptr;

    const bool srgb = samplerIsSrgb(reference.sampler);

    Spec::TextureOptions options;
    options.resourceExists = true;
    options.isChild = true;
    options.sampler = reference.sampler;
    options.existingResource = childKey(reference.sampler);
    options.source = relative;
    options.target = mpp::TextureTarget::Texture2D;
    options.params = defaultTextureParams(srgb);
    specification.textures.push_back(options);

    pending.push_back({childKey(reference.sampler), reference.sampler, relative, srgb});
  }

  // No ResourceManager: nothing here loads an image or touches the GPU, and the write path only
  // needs the declarative state. The image-load function is attached by MppModelStream at load
  // time, after deserialization.
  auto stream = std::make_shared<mpp::ProgrammaticPbrMaterialStream>(nullptr);
  stream->setSpecification(specification);

  for (const PendingTexture& texture : pending) {
    auto textureStream = std::make_shared<mpp::ProgrammaticTextureStream>(nullptr);
    textureStream->setTarget(mpp::TextureTarget::Texture2D);
    textureStream->setParams(defaultTextureParams(texture.srgb));
    textureStream->setColourSpace(texture.srgb ? mpp::TextureColourSpace::Srgb : mpp::TextureColourSpace::Linear);
    textureStream->setSampler(texture.sampler);
    textureStream->setFile(texture.relativePath, nullptr);
    stream->addChild(texture.key, textureStream);
  }

  // These children are the finished article, not a starting point: mark them created so that if
  // this stream is ever loaded in-process (rather than round-tripped through a file, where
  // ModelSerializer::readMaterial does the same thing) ProgrammaticPbrMaterialStream doesn't
  // rebuild them from TextureOptions::source and lose any base-path fix-up.
  stream->_markChildrenCreated();

  return stream;
}

}  // namespace modelio
