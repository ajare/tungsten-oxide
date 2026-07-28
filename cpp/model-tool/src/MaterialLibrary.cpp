#include "MaterialLibrary.hpp"

#include <mpp/ProgrammaticMaterialStream.h>
#include <mpp/ProgrammaticTextureStream.h>
#include <mpp/ResourceManager.h>
#include <mpp/ResourceWrangler.h>

#include "ModelResources.hpp"
#include "TextureLoad.hpp"

namespace modeltool {

MaterialLibrary::MaterialLibrary(mpp::ResourceManager& resourceMgr, mpp::ResourceWrangler& wrangler)
    : resourceMgr_(resourceMgr), wrangler_(wrangler) {}

MaterialLibrary::~MaterialLibrary() {
  while (!materials_.empty()) releaseAndErase(materials_.begin());

  if (defaultFallbackMaterial_) {
    defaultFallbackMaterial_->release(&wrangler_);
    if (defaultFallbackMaterial_->getRefCount() == 0) resourceMgr_.deleteResource(defaultFallbackMaterial_->getName());
    defaultFallbackMaterial_.reset();
  }
}

mpp::ResourcePtr MaterialLibrary::defaultFallbackMaterial() {
  if (defaultFallbackMaterial_) return defaultFallbackMaterial_;

  const mpp::mesh::MeshSpecification meshSpec = fixedMeshSpecification();
  auto* matStream = new mpp::ProgrammaticMaterialStream(&resourceMgr_);
  matStream->setProgram2d(false);
  matStream->setMeshSpecification(meshSpec);
  matStream->setTexture("TEX1", "__mpp_tex_none__");

  defaultFallbackMaterial_ = resourceMgr_.declareResource("ModelTool.DefaultFallbackMaterial3D", mpp::ResourceStreamPtr(matStream)).first;
  defaultFallbackMaterial_->acquire(&wrangler_);
  defaultFallbackMaterial_->load();
  return defaultFallbackMaterial_;
}

void MaterialLibrary::releaseAndErase(std::map<std::string, LoadedMaterial>::iterator it) {
  // Release+delete the Material before the Texture: Material::createImpl() internally acquires
  // whatever Texture its "TEX1" sampler resolves to (Material.cpp), so the Texture's own
  // getDependingObjectCount() only drops to 0 once the Material's external release() triggers its
  // releaseDependentResources() -- deleting the Texture first would throw ("still has
  // references"). Mirrors mpp::Batch::~Batch()'s own release-then-delete idiom (Batch.cpp).
  LoadedMaterial material = std::move(it->second);
  materials_.erase(it);

  if (material.materialResource) {
    material.materialResource->release(&wrangler_);
    if (material.materialResource->getRefCount() == 0) resourceMgr_.deleteResource(material.qualifiedName);
  }
  if (material.textureResource) {
    const std::string textureName = material.textureResource->getName();
    material.textureResource->release(&wrangler_);
    if (material.textureResource->getRefCount() == 0) resourceMgr_.deleteResource(textureName);
  }
}

MaterialReference MaterialLibrary::declareCommon(const std::string& qualifiedName, mpp::ResourceStreamPtr materialStream,
                                                  mpp::ResourcePtr textureResource, const std::optional<std::string>& displayTexturePath,
                                                  const std::string& sourceFile, MaterialProvenance provenance, int initialRefCount) {
  auto existing = materials_.find(qualifiedName);
  if (existing != materials_.end()) releaseAndErase(existing);

  LoadedMaterial material;
  material.qualifiedName = qualifiedName;
  material.sourceFile = sourceFile;
  material.texturePath = displayTexturePath;
  material.provenance = provenance;
  material.modelRefCount = initialRefCount;
  material.instanceId = nextInstanceId_++;
  material.textureResource = std::move(textureResource);

  material.materialResource = resourceMgr_.declareResource(qualifiedName, std::move(materialStream)).first;
  material.materialResource->acquire(&wrangler_);
  material.materialResource->load();

  const MaterialReference ref{qualifiedName, material.instanceId};
  materials_[qualifiedName] = std::move(material);
  return ref;
}

MaterialReference MaterialLibrary::declare(const std::string& qualifiedName, const std::optional<std::string>& texturePath,
                                            const std::string& sourceFile, MaterialProvenance provenance, int initialRefCount) {
  const int textureGeneration = textureGeneration_++;
  const mpp::mesh::MeshSpecification meshSpec = fixedMeshSpecification();

  std::string textureName = "__mpp_tex_none__";
  mpp::ResourcePtr textureResource;
  if (texturePath.has_value()) {
    textureName = "ModelTool.MaterialLibraryTexture." + std::to_string(textureGeneration);
    auto* texStream = new mpp::ProgrammaticTextureStream(&resourceMgr_);
    // setTarget() before setFile() -- same load-bearing precondition documented in
    // ModelResources.cpp (a missing target crashes on upload, not just renders wrong).
    texStream->setTarget(mpp::TextureTarget::Texture2D);
    texStream->setFiltering(mpp::TextureParams::MinFilter::Linear, mpp::TextureParams::MagFilter::Linear);
    texStream->setFile(*texturePath, &loadImage);
    textureResource = resourceMgr_.declareResource(textureName, mpp::ResourceStreamPtr(texStream)).first;
    textureResource->acquire(&wrangler_);
    textureResource->load();
  }

  auto* matStream = new mpp::ProgrammaticMaterialStream(&resourceMgr_);
  matStream->setProgram2d(false);
  matStream->setMeshSpecification(meshSpec);
  matStream->setTexture("TEX1", textureName);

  return declareCommon(qualifiedName, mpp::ResourceStreamPtr(matStream), textureResource, texturePath, sourceFile, provenance, initialRefCount);
}

void MaterialLibrary::addUserImported(const std::string& qualifiedName, const std::optional<std::string>& texturePath,
                                       const std::string& sourceFile) {
  declare(qualifiedName, texturePath, sourceFile, MaterialProvenance::UserImported, 0);
}

MaterialReference MaterialLibrary::declareModelOwned(const std::string& qualifiedName, const std::optional<std::string>& texturePath,
                                                      const std::string& sourceFile) {
  return declare(qualifiedName, texturePath, sourceFile, MaterialProvenance::ModelOwned, 1);
}

MaterialReference MaterialLibrary::declareModelOwnedFromStream(const std::string& qualifiedName, mpp::ResourceStreamPtr stream,
                                                                 const std::optional<std::string>& displayTexturePath,
                                                                 const std::string& sourceFile) {
  return declareCommon(qualifiedName, std::move(stream), mpp::ResourcePtr(), displayTexturePath, sourceFile, MaterialProvenance::ModelOwned, 1);
}

std::optional<MaterialReference> MaterialLibrary::acquireExistingReference(const std::string& qualifiedName) {
  auto it = materials_.find(qualifiedName);
  if (it == materials_.end()) return std::nullopt;
  ++it->second.modelRefCount;
  return MaterialReference{qualifiedName, it->second.instanceId};
}

void MaterialLibrary::releaseModelReference(const MaterialReference& ref) {
  auto it = materials_.find(ref.qualifiedName);
  // A different instanceId means this exact entry was superseded (Replace) since the reference
  // was acquired -- that old reference is already moot, and the replacement's own refcount was
  // never affected by it, so there's nothing to release here.
  if (it == materials_.end() || it->second.instanceId != ref.instanceId) return;

  --it->second.modelRefCount;
  if (it->second.modelRefCount <= 0 && it->second.provenance == MaterialProvenance::ModelOwned) releaseAndErase(it);
}

void MaterialLibrary::remove(const std::string& qualifiedName) {
  auto it = materials_.find(qualifiedName);
  if (it == materials_.end()) return;
  releaseAndErase(it);
}

}  // namespace modeltool
