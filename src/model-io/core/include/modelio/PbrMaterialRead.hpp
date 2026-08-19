// PbrMaterialRead.hpp — an embedded mpp PbrMaterial resource stream -> MaterialData, the inverse
// of PbrMaterialBuild.hpp's write path.
//
// mpp::PbrMaterialStream::getPbrSurface() (PbrMaterialSpecification::PbrSurface: baseColourFactor,
// metallicFactor, roughnessFactor, emissiveFactor, normalScale, occlusionStrength, alphaMode,
// alphaCutoff, doubleSided) is a field-for-field match with MaterialData, and getTextures() gives
// back each TextureOptions::source exactly as PbrMaterialBuild.cpp wrote it -- a path relative to
// the .mppmodel's own directory, per RSE4's own on-disk contract.
#pragma once

#include <filesystem>

#include <mpp/ResourceStream.h>

#include "modelio/ModelData.hpp"

namespace modelio {

// Returns false (no report -- this is an expected, non-error outcome) when `stream` is not an
// mpp::PbrMaterialStream, e.g. a legacy mpp::BasicMaterialStream. Callers fall back to a name-only
// material in that case. `out.name` is left untouched; the caller sets it.
bool readEmbeddedPbrMaterial(const mpp::ResourceStreamPtr& stream, const std::filesystem::path& modelDirectory,
                             MaterialData& out);

}  // namespace modelio
