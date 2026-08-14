#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/ResourceWrangler.h>

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/ResourceLocation.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/DataStream.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
class WP_APPLICATION_API Resource : public mpp::ResourceWrangler {
  friend class ResourceManager;

private:
  typedef std::pair<std::string, StructuredData> FactoryTypeDefinition;

private:
  int mRefCount;

  std::string mName;

  std::string mNamespace;

  std::string mType;

  std::string mSource;

  std::vector<FactoryTypeDefinition> mDefinitions;

  std::map<std::string, std::shared_ptr<Resource>> mNamedDependentResources;

  std::vector<std::shared_ptr<Resource>> mDependentResourceList;

  std::map<std::string, std::string> mTags;

  // Definition factories
  static std::map<std::string, std::map<std::string, ResourceDefinitionFactory*>> msResourceDefinitionFactories;

protected:
  bool mCreated, mLoaded;

  ResourceLocation* mwLocation;

  mpp::ResourcePtr mMppResource;

private:
  ResourceDefinitionFactory* getResourceDefinitionFactory(std::string const& resType, std::string const& facType, bool errorIfNotFound = true) const;

  void addDefinition(std::string const& factory, StructuredData const& definition);

  virtual void create(DataStreamPtr dataPtr, ResourceManager* resourceMgr);

  virtual void destroy();

  virtual bool load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);

  virtual bool unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);

protected:
  void addDependentResource(std::shared_ptr<Resource> resource);

  void addDependentResource(std::string const& id, std::shared_ptr<Resource> resource);

  void parseDefinition(ResourceManager* resourceMgr);

  virtual void parseData(DataStreamPtr dataPtr);

public:
  Resource(std::string const& name, std::string const& namesp, std::string const& type, std::string const& source, std::map<std::string, std::string> const& tags, ResourceLocation* location);

  virtual ~Resource() = default;

  void addTag(std::string const& name, std::string const& value);

  std::string const& getTag(std::string const& name) const;

  std::map<std::string, std::string> const& getTags() const;

  std::string const& getName() const;

  std::string const& getNamespace() const;

  std::string getQualifiedName() const;

  std::string const& getType() const;

  std::string const& getSource() const;

  std::string const& getDefinitionFile() const;

  bool hasDependentResource(std::string const& id) const;

  std::shared_ptr<Resource> getDependentResource(std::string const& id);

  virtual mpp::ResourcePtr getMppResource() const;

  static void splitName(std::string const& qualifiedName, std::string const& currentNamesp, std::string* namesp = nullptr, std::string* resource = nullptr);
};

typedef std::shared_ptr<Resource> ResourcePtr;

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
