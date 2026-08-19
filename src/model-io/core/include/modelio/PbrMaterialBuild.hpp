// PbrMaterialBuild.hpp — MaterialData -> an embedded mpp PbrMaterial resource stream.
//
// Surface values and maps come from the source asset; the program and MeshSpecification come from
// the pipeline material the caller named (docs/adr/0004-gltf-import.md, D4).
//
// Textures are emitted as *child* TextureStreams under "Textures/<SAMPLER>", matching exactly what
// FilePbrMaterialStream produces for a pipeline <BaseColourMap><Resource>. That shape is load-
// bearing, not cosmetic: MppModelStream rebases relative paths only on an embedded material's
// children, and ModelSerializer::readMaterial marks those children as already created so
// ProgrammaticPbrMaterialStream doesn't rebuild (and un-rebase) them. An inline
// TextureOptions::source would instead resolve against the process CWD -- see D5.
//
// Colour space is set per sampler (base colour and emissive are sRGB; normal, metallic-roughness
// and occlusion are linear) and survives serialization from resource-stream version RSE4 onward.
// RSE3 and earlier had no field for it, so an embedded sRGB map written by those versions reloads
// as TextureParams' Linear default; the format was extended in the MassivePolyPusher submodule as
// part of this work rather than shipping silently mis-shaded imports.
#pragma once

#include <filesystem>
#include <string>

#include <mpp/ResourceStream.h>

#include "modelio/Diagnostics.hpp"
#include "modelio/ModelData.hpp"
#include "modelio/PipelineMaterial.hpp"

namespace modelio {

// The colour space a given PBR sampler should be sampled in.
bool samplerIsSrgb(const std::string& sampler);

// Returns null (having reported) if any texture cannot be expressed as a path relative to
// `modelDirectory` -- no relative path can escape the model's own directory tree, so a texture
// outside it would silently resolve somewhere else on another machine.
mpp::ResourceStreamPtr buildEmbeddedPbrMaterial(const MaterialData& material, const TargetMaterial& target,
                                                const std::filesystem::path& modelDirectory, Report& report);

}  // namespace modelio
