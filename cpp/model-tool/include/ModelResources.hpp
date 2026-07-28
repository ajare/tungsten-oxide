// ModelResources.hpp — builds live mpp resources (Materials, Model) from an ImportedModel, shared
// by the viewport (which renders them) and MppSave (which serializes the same material streams
// into the saved .mppmodel). See docs/adr/0001-model-tool.md, D5.
//
// No custom shader/Program is declared here, despite ADR 0001 D6 describing "one small bundled
// GLSL program": mpp::RenderSystem::createCoreResources() already declares a core Program resource
// ("__mpp_p3d_tris_p3n3t2c4__") with exactly this fixed vertex layout (Position3/Normal3/
// TexCoord2/Colour4) and exactly the shading D6 asked for (ambient + N lights, diffuse + specular,
// modulated by a diffuse sampler) -- see mpp/DefaultShaders.h's FragmentShader3dTemplate. A
// ProgrammaticMaterialStream that never calls setProgram() (just setProgram2d(false)) resolves to
// it automatically via ResourceManager::getDefault3dProgram() (see mpp/src/Material.cpp), which is
// exactly the pattern StatePlayTungstenMonoxide::createTorusMaterial() already uses. Writing a new
// Program via mpp::program::Parser turned out to be unnecessary.
//
// No willpower.application Resource/XML system is used anywhere here (ADR D1) -- everything is
// built programmatically against bare mpp::ResourceManager, the same way
// ext/massivepolypusher/demo-suite's ModelScene and StatePlayTungstenMonoxide::createTorusModel()
// already do.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <mpp/Resource.h>
#include <mpp/ResourceStream.h>

#include "AssImpImport.hpp"

namespace mpp {
class ResourceManager;
class ResourceWrangler;
}  // namespace mpp

namespace modeltool {

struct BuiltMaterial {
  std::string name;                   // the resource name this material was declared under
  mpp::ResourceStreamPtr stream;      // the live ProgrammaticMaterialStream, kept for MppSave's addMaterial()
  mpp::ResourcePtr materialResource;   // the declared Resource wrapping `stream`, for release
  mpp::ResourcePtr textureResource;    // non-null only when a real (non-sentinel) texture was declared
};

struct BuiltModel {
  mpp::ResourcePtr modelResource;        // the live Model resource, added to the Scene
  std::vector<BuiltMaterial> materials;  // parallel to ImportedModel::materials
  ImportedModel source;                  // retained for MppSave (mesh names/vertices/indices)
};

// Builds one Material resource per ImportedModel::materials entry (named texture when present,
// mpp's built-in "__mpp_tex_none__" sentinel otherwise -- ADR D7) plus one Model resource
// referencing them, and acquires+loads all of it against `wrangler`. The caller is responsible for
// adding the returned BuiltModel::modelResource to a Scene and eventually calling
// releaseBuiltModel().
BuiltModel buildModel(mpp::ResourceManager& resourceMgr, mpp::ResourceWrangler& wrangler, ImportedModel imported);

// Releases every resource buildModel() acquired against `wrangler` (model, materials, and any
// per-material textures).
void releaseBuiltModel(BuiltModel& built, mpp::ResourceWrangler& wrangler);

// Packs ImportedVertex into the fixed 36-byte layout (position f32x3, normal f32x3, uv f32x2,
// colour unorm8x4) -- shared by buildModel()'s live GPU upload and MppSave's file serialization,
// so the byte layout is defined in exactly one place.
std::vector<std::uint8_t> packVertices(const std::vector<ImportedVertex>& vertices);

}  // namespace modeltool
