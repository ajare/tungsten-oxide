// ModelResources.hpp — builds a live mpp Model resource from an ImportedModel. Unlike the
// original version of this file, materials are NOT owned privately per model: every material
// entry (whether AssImp-embedded or .mppmodel-embedded/externally-referenced/defaulted) is
// resolved through MaterialLibrary (see MaterialLibrary.hpp) by main.cpp *before* buildModel() is
// called -- buildModel() only does final assembly against already-resolved MaterialReferences, so
// it never needs to know about name collisions/conflict resolution itself.
//
// No custom shader/Program is declared here, despite ADR 0001 D6 describing "one small bundled
// GLSL program": mpp::RenderSystem::createCoreResources() already declares a core Program resource
// ("__mpp_p3d_tris_p3n3t2c4__") with exactly this fixed vertex layout (Position3/Normal3/
// TexCoord2/Colour4) and exactly the shading D6 asked for (ambient + N lights, diffuse + specular,
// modulated by a diffuse sampler) -- see mpp/DefaultShaders.h's FragmentShader3dTemplate. A
// ProgrammaticMaterialStream that never calls setProgram() (just setProgram2d(false)) resolves to
// it automatically via ResourceManager::getDefault3dProgram() (see mpp/src/Material.cpp), which is
// exactly the pattern StatePlayTungstenMonoxide::createTorusMaterial() already uses.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <mpp/Resource.h>
#include <mpp/ResourceStream.h>
#include <mpp/mesh/MeshSpecification.h>

#include "AssImpImport.hpp"
#include "MaterialLibrary.hpp"

namespace mpp {
class ResourceManager;
class ResourceWrangler;
}  // namespace mpp

namespace modeltool {

struct BuiltModel {
  mpp::ResourcePtr modelResource;  // the live Model resource, added to the Scene
  // Parallel to source.materials -- nullopt for a DefaultFallback entry (which references
  // model-tool's own shared default-white material, never torn down per-model). Released via
  // MaterialLibrary::releaseModelReference() in releaseBuiltModel().
  std::vector<std::optional<MaterialReference>> materialRefs;
  ImportedModel source;  // retained for MppSave (mesh names/vertices/indices) and the left panel
};

// Builds one Model resource from `imported`. `materialRefs` must be parallel to
// `imported.materials` (same size), already resolved by the caller: a real MaterialReference for
// every Embedded/ExternalReference entry (see MaterialLibrary::declareModelOwned()/
// acquireExistingReference()), nullopt for every DefaultFallback entry. `defaultFallbackMaterialName`
// is the resource name a DefaultFallback mesh's material resolves to (see
// MaterialLibrary::defaultFallbackMaterial()).
BuiltModel buildModel(mpp::ResourceManager& resourceMgr, mpp::ResourceWrangler& wrangler, ImportedModel imported,
                       std::vector<std::optional<MaterialReference>> materialRefs, const std::string& defaultFallbackMaterialName);

// Releases the Model resource and every material reference buildModel() was given, via
// `materialLibrary` (so a ModelOwned material whose refcount reaches zero is properly cleaned up).
void releaseBuiltModel(BuiltModel& built, mpp::ResourceWrangler& wrangler, MaterialLibrary& materialLibrary);

// Packs ImportedVertex into the fixed 36-byte layout (position f32x3, normal f32x3, uv f32x2,
// colour unorm8x4) -- shared by buildModel()'s live GPU upload, MppSave's file serialization, and
// MppModelImport's unpacking of a loaded .mppmodel's own vertex streams.
std::vector<std::uint8_t> packVertices(const std::vector<ImportedVertex>& vertices);

// The MeshSpecification every ProgrammaticMaterialStream this app declares is built against (see
// this header's top comment) -- shared with MaterialLibrary.cpp, which declares materials outside
// of any particular model's build, but still needs the identical spec so its materials resolve to
// the same core default 3D program.
mpp::mesh::MeshSpecification fixedMeshSpecification();

}  // namespace modeltool
