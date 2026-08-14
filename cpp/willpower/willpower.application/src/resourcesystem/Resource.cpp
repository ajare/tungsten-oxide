#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/application/resourcesystem/Resource.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;

map<string, map<string, ResourceDefinitionFactory*>> Resource::msResourceDefinitionFactories;

Resource::Resource(string const& name, string const& namesp, string const& type, string const& source, std::map<string, string> const& tags, ResourceLocation* location)
    : ResourceWrangler(name), mRefCount(0), mName(name), mNamespace(namesp), mType(type), mSource(source), mTags(tags), mwLocation(location), mCreated(false), mLoaded(false) {
}

void Resource::create(DataStreamPtr dataPtr, ResourceManager* resourceMgr) {
  parseData(dataPtr);
  parseDefinition(resourceMgr);
}

void Resource::destroy() {
}

bool Resource::load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(renderSystem);
  WP_UNUSED(resourceMgr);

  return true;
};

bool Resource::unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(renderSystem);
  WP_UNUSED(resourceMgr);

  return true;
};

void Resource::addTag(string const& name, string const& value) {
  mTags[name] = value;
}

string const& Resource::getTag(string const& name) const {
  return mTags.at(name);
}

map<string, string> const& Resource::getTags() const {
  return mTags;
}

void Resource::addDependentResource(shared_ptr<Resource> resource) {
  mDependentResourceList.push_back(resource);
}

void Resource::addDependentResource(string const& id, shared_ptr<Resource> resource) {
  mNamedDependentResources[id] = resource;
  addDependentResource(resource);
}

bool Resource::hasDependentResource(string const& id) const {
  return mNamedDependentResources.find(id) != mNamedDependentResources.end();
}

ResourcePtr Resource::getDependentResource(string const& id) {
  auto it = mNamedDependentResources.find(id);

  if (it == mNamedDependentResources.end()) {
    throw ResourceException(this, "could not get dependent resource '" + id + "'.");
  }

  return it->second;
}

ResourceDefinitionFactory* Resource::getResourceDefinitionFactory(string const& resType, string const& facType, bool errorIfNotFound) const {
  auto it1 = msResourceDefinitionFactories.find(resType);
  if (it1 == msResourceDefinitionFactories.end()) {
    if (errorIfNotFound) {
      throw ResourceSystemException("DefinitionFactories for resource type '" + resType + "' not registered.");
    } else {
      return nullptr;
    }
  }

  auto innerMap = it1->second;
  auto it2 = innerMap.find(facType);
  if (it2 == innerMap.end()) {
    if (errorIfNotFound) {
      throw ResourceSystemException("DefinitionFactory for resource type '" + resType + "', factory type '" + facType + "' not registered.");
    } else {
      return nullptr;
    }
  }

  return it2->second;
}

void Resource::parseDefinition(ResourceManager* resourceMgr) {
  for (auto const& def : mDefinitions) {
    auto const& [facType, definition] = def;

    auto fac = getResourceDefinitionFactory(getType(), facType, false);
    if (fac) {
      DataNode node(definition);
      fac->create(this, resourceMgr, &node);
      return;
    }
  }

  throw ResourceException(this, "could not find a definition factory.");
}

void Resource::parseData(DataStreamPtr dataPtr) {
}

void Resource::addDefinition(string const& factory, StructuredData const& definition) {
  mDefinitions.push_back(make_pair(factory, definition));
}

string const& Resource::getName() const {
  return mName;
}

string const& Resource::getNamespace() const {
  return mNamespace;
}

string Resource::getQualifiedName() const {
  if (mNamespace == "") {
    return getName();
  } else {
    return getNamespace() + "/" + getName();
  }
}

string const& Resource::getType() const {
  return mType;
}

string const& Resource::getSource() const {
  return mSource;
}

string const& Resource::getDefinitionFile() const {
  return mwLocation->getDefinitionFile();
}

mpp::ResourcePtr Resource::getMppResource() const {
  return mMppResource;
}

void Resource::splitName(string const& qualifiedName, string const& currentNamesp, string* namesp, string* resource) {
  string delim = "/";

  auto index = qualifiedName.find(delim);
  if (index == qualifiedName.npos) {
    if (namesp) {
      *namesp = currentNamesp;
    }
    if (resource) {
      *resource = qualifiedName;
    }
  } else {
    if (namesp) {
      if (index == 0) {
        *namesp = "";
      } else {
        *namesp = qualifiedName.substr(0, index);
      }
    }
    if (resource) {
      *resource = qualifiedName.substr(index + delim.length());
    }
  }
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE