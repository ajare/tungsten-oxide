#include "modelio/PbrMaterialRead.hpp"

#include <mpp/PbrMaterialStream.h>

namespace modelio {
namespace {

AlphaMode alphaModeOf(mpp::PbrMaterialSpecification::PbrAlphaMode mode) {
  switch (mode) {
    case mpp::PbrMaterialSpecification::PbrAlphaMode::Mask: return AlphaMode::Mask;
    case mpp::PbrMaterialSpecification::PbrAlphaMode::Blend: return AlphaMode::Blend;
    case mpp::PbrMaterialSpecification::PbrAlphaMode::Opaque: break;
  }
  return AlphaMode::Opaque;
}

}  // namespace

bool readEmbeddedPbrMaterial(const mpp::ResourceStreamPtr& stream, const std::filesystem::path& modelDirectory,
                             MaterialData& out) {
  auto pbrStream = std::dynamic_pointer_cast<mpp::PbrMaterialStream>(stream);
  if (!pbrStream) return false;

  const mpp::PbrMaterialSpecification::PbrSurface& surface = pbrStream->getPbrSurface();
  out.baseColourFactor[0] = surface.baseColourFactor.r;
  out.baseColourFactor[1] = surface.baseColourFactor.g;
  out.baseColourFactor[2] = surface.baseColourFactor.b;
  out.baseColourFactor[3] = surface.baseColourFactor.a;
  out.metallicFactor = surface.metallicFactor;
  out.roughnessFactor = surface.roughnessFactor;
  out.emissiveFactor[0] = surface.emissiveFactor.r;
  out.emissiveFactor[1] = surface.emissiveFactor.g;
  out.emissiveFactor[2] = surface.emissiveFactor.b;
  out.normalScale = surface.normalScale;
  out.occlusionStrength = surface.occlusionStrength;
  out.alphaMode = alphaModeOf(surface.alphaMode);
  out.alphaCutoff = surface.alphaCutoff;
  out.doubleSided = surface.doubleSided;

  out.textures.clear();
  for (const mpp::PbrMaterialSpecification::TextureOptions& texture : pbrStream->getTextures()) {
    if (texture.source.empty()) continue;
    // TextureOptions::source is relative to the .mppmodel's own directory, exactly as
    // PbrMaterialBuild.cpp wrote it (see this header's own comment).
    const std::filesystem::path absolute = (modelDirectory / texture.source).lexically_normal();
    out.textures.push_back({texture.sampler, absolute.string()});
  }

  return true;
}

}  // namespace modelio
