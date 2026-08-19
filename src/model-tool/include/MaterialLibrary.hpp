// MaterialLibrary.hpp — the single catalog of every live mpp Material (+ optional Texture)
// resource this app knows about, regardless of where it came from: "Import Materials XML..."
// (see MaterialXmlImport.hpp), AssImp model import, or .mppmodel import (see MppModelImport.hpp).
// Each entry is declared under its own qualified resource name (e.g. "MyNamespace/MyMaterial", or
// for a model's own materials, "<model-filename-stem>/<material-name>") so importing the same
// name twice is detectable (contains()) before committing anything -- unlike
// willpower.application::ResourceManager's own XML-driven loading, which silently overwrites
// same-named resources with no collision signal at all (see MaterialXmlImport.hpp's comment).
//
// Every entry carries a MaterialProvenance:
//   - UserImported: came from the explicit "Import Materials XML..." flow. Never auto-removed --
//     only the left panel's Unload button (remove()) takes it out, regardless of how many models
//     reference it.
//   - ModelOwned: created as a side effect of loading a model (an embedded material in an AssImp
//     source or a .mppmodel). Carries a model-reference count: every currently-loaded model that
//     uses it (declareModelOwned() for the model that created it, acquireExistingReference() for
//     any other model that merely references it by name) holds one MaterialReference token: a
//     (qualifiedName, instanceId) pair. Releasing the last one (releaseModelReference()) auto-
//     removes a ModelOwned entry; a UserImported entry is never auto-removed this way.
//
// The instanceId in MaterialReference exists so a stale release is safely a no-op: if a name
// collision was resolved with "Replace" while some other model still held a reference to the
// pre-replace entry, that old reference's instanceId no longer matches what's currently declared
// under the name, so releasing it does nothing (the pre-replace entry is already gone; the
// replacement's own reference count is unaffected by a reference to the resource it superseded).
//
// Rendering-wise every declared Material is built exactly like ModelResources.cpp's construction
// (same fixedMeshSpecification()/default-3D-program/TEX1-sampler convention) -- this app only ever
// uses the one fixed vertex layout and shading, regardless of a material's origin.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include <mpp/Resource.h>
#include <mpp/ResourceStream.h>

namespace mpp {
class ResourceManager;
class ResourceWrangler;
}  // namespace mpp

namespace modeltool {

enum class MaterialProvenance { UserImported, ModelOwned };

struct LoadedMaterial {
  std::string qualifiedName;
  std::string sourceFile;                  // Resources.xml or model file this came from, for display
  std::optional<std::string> texturePath;  // nullopt -> untextured (mpp's __mpp_tex_none__ sentinel)
  MaterialProvenance provenance{MaterialProvenance::UserImported};
  int modelRefCount{0};      // ModelOwned only: how many currently-loaded models reference this by name
  std::uint64_t instanceId{0};
  mpp::ResourcePtr materialResource;
  mpp::ResourcePtr textureResource;  // null when texturePath is nullopt
};

// A token handed back when a model acquires a reference to a material; pass it back to
// releaseModelReference() when that model is replaced/torn down. See this header's top comment.
struct MaterialReference {
  std::string qualifiedName;
  std::uint64_t instanceId{0};
};

class MaterialLibrary {
 public:
  MaterialLibrary(mpp::ResourceManager& resourceMgr, mpp::ResourceWrangler& wrangler);
  ~MaterialLibrary();

  MaterialLibrary(const MaterialLibrary&) = delete;
  MaterialLibrary& operator=(const MaterialLibrary&) = delete;

  bool contains(const std::string& qualifiedName) const { return materials_.find(qualifiedName) != materials_.end(); }

  // Declares a UserImported material, replacing any existing entry of the same name. Callers
  // (doImportMaterialsXml's Replace/Ignore flow) are responsible for only calling this once the
  // user has actually chosen to replace a colliding name -- see contains().
  void addUserImported(const std::string& qualifiedName, const std::optional<std::string>& texturePath, const std::string& sourceFile);

  // Declares a ModelOwned material with an initial model-reference count of 1 (the model that's
  // declaring it), replacing any existing entry of the same name. Returns a token the caller
  // stores on the model and later passes to releaseModelReference() when that model is replaced.
  MaterialReference declareModelOwned(const std::string& qualifiedName, const std::optional<std::string>& texturePath,
                                      const std::string& sourceFile);

  // Same as declareModelOwned(), but for an ALREADY-BUILT ResourceStream rather than one built
  // fresh from a raw texture file path -- used for a .mppmodel's own embedded materials, which
  // arrive as a fully-deserialized mpp::ResourceStreamPtr (via mpp::ModelSerializer::readMaterial(),
  // see MppModelImport.hpp) whose texture binding is already reconstructed internally.
  // `displayTexturePath` is best-effort left-panel metadata only (nullopt if not extracted) -- the
  // actual texture lives inside `stream` itself, not as a separately-tracked Resource.
  MaterialReference declareModelOwnedFromStream(const std::string& qualifiedName, mpp::ResourceStreamPtr stream,
                                                 const std::optional<std::string>& displayTexturePath, const std::string& sourceFile);

  // Looks up an already-loaded material (either provenance) by name and bumps its model-reference
  // count by one -- for a mesh whose material ISN'T embedded in its own file but names one that's
  // already loaded. Returns nullopt if `qualifiedName` isn't currently loaded.
  std::optional<MaterialReference> acquireExistingReference(const std::string& qualifiedName);

  // Decrements the model-reference count identified by `ref`. No-op if the entry currently loaded
  // under `ref.qualifiedName` has a different instanceId (superseded by a Replace since this
  // reference was acquired). A ModelOwned entry whose count reaches zero is fully released and
  // un-declared; a UserImported entry is never auto-removed by this.
  void releaseModelReference(const MaterialReference& ref);

  // Explicit user action (left panel's Unload button). UI callers should disable this for a
  // ModelOwned entry still in use (modelRefCount > 0) -- see main.cpp -- though mpp's own resource
  // system would in any case safely refuse an unsafe removal.
  void remove(const std::string& qualifiedName);

  const std::map<std::string, LoadedMaterial>& materials() const { return materials_; }

  // model-tool's single shared "default white" 3D material -- used as a mesh's material when a
  // .mppmodel references a name that isn't embedded and isn't currently loaded (see
  // MppModelImport.hpp). Declared lazily on first call and kept alive for this MaterialLibrary's
  // own lifetime (released in the destructor, same as everything else it owns) -- intentionally
  // NOT part of materials()/the left panel display, since it's an internal engine-wide fallback,
  // not something the user imported or that any specific model owns, and it's shared by every
  // model that needs it rather than declared fresh per use.
  mpp::ResourcePtr defaultFallbackMaterial();

 private:
  mpp::ResourceManager& resourceMgr_;
  mpp::ResourceWrangler& wrangler_;
  std::map<std::string, LoadedMaterial> materials_;
  int textureGeneration_{0};
  std::uint64_t nextInstanceId_{1};
  mpp::ResourcePtr defaultFallbackMaterial_;

  MaterialReference declare(const std::string& qualifiedName, const std::optional<std::string>& texturePath, const std::string& sourceFile,
                            MaterialProvenance provenance, int initialRefCount);
  MaterialReference declareCommon(const std::string& qualifiedName, mpp::ResourceStreamPtr materialStream, mpp::ResourcePtr textureResource,
                                  const std::optional<std::string>& displayTexturePath, const std::string& sourceFile,
                                  MaterialProvenance provenance, int initialRefCount);
  void releaseAndErase(std::map<std::string, LoadedMaterial>::iterator it);
};

}  // namespace modeltool
