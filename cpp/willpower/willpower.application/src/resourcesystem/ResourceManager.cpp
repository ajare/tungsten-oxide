#include <mpp/TextureStream.h>

#include "willpower/common/StringUtils.h"

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/application/resourcesystem/ResourceManager.h"
#include "willpower/application/resourcesystem/ImageResource.h"
#include "willpower/application/resourcesystem/ImageSetResource.h"
#include "willpower/application/resourcesystem/AnimationSetResource.h"
#include "willpower/application/resourcesystem/ProgramResource.h"
#include "willpower/application/resourcesystem/ShaderResource.h"
#include "willpower/application/resourcesystem/MaterialResource.h"
#include "willpower/application/resourcesystem/TextFileResource.h"
#include "willpower/application/resourcesystem/XmlFileResource.h"
#include "willpower/application/resourcesystem/AudioBankResource.h"
#include "willpower/application/resourcesystem/AnimationSetDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ImageDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ImageSetDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ProgramDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ShaderDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/MaterialDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/TextFileDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/XmlFileDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/AudioBankDefaultDefinitionFactory.h"


namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			ResourceManager::ResourceManager(mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, AudioSystem* audioSystem, Logger* logger)
				: mwLogger(logger)
				, mwRenderSystem(renderSystem)
				, mwRenderResourceMgr(renderResourceMgr)
				, mwAudioSystem(audioSystem)

			{
				addResourceFactory(new TextFileResourceFactory);
				addResourceFactory(new XmlFileResourceFactory);
				addResourceFactory(new ImageResourceFactory);
				addResourceFactory(new ImageSetResourceFactory);
				addResourceFactory(new AnimationSetResourceFactory);
				addResourceFactory(new ShaderResourceFactory);
				addResourceFactory(new ProgramResourceFactory);
				addResourceFactory(new MaterialResourceFactory);
				addResourceFactory(new AudioBankResourceFactory(audioSystem));

				addResourceDefinitionFactory(new TextFileDefaultDefinitionFactory);
				addResourceDefinitionFactory(new XmlFileDefaultDefinitionFactory);
				addResourceDefinitionFactory(new AnimationSetDefaultDefinitionFactory);
				addResourceDefinitionFactory(new ImageDefaultDefinitionFactory);
				addResourceDefinitionFactory(new ImageSetDefaultDefinitionFactory);
				addResourceDefinitionFactory(new ShaderDefaultDefinitionFactory);
				addResourceDefinitionFactory(new ProgramDefaultDefinitionFactory);
				addResourceDefinitionFactory(new MaterialDefaultDefinitionFactory);
				addResourceDefinitionFactory(new AudioBankDefaultDefinitionFactory);
			}

			ResourceManager::~ResourceManager()
			{
				// Destroy resource locations
				for (auto record : mLocations)
				{
					delete record.location;
				}

				// Destroy all resources
				for (auto nmspIt : mResources)
				{
					for (auto it : nmspIt.second)
					{
						auto res = it.second;

						if (res->mLoaded)
						{
							_unloadResource(res);
						}

						_destroyResource(it.second);
					}
				}

				// Destroy all resource factories
				for (auto fac : mResourceFactories)
				{
					delete fac.second;
				}

				for (auto const& fItem : Resource::msResourceDefinitionFactories)
				{
					auto const&[resType, mapping] = fItem;
					for (auto const& mItem : mapping)
					{
						auto const&[facType, factory] = mItem;
						delete factory;
					}
				}
			}

			vector<string> ResourceManager::sortResourcesByDependency(vector<string> const& resourceNames, map<string, vector<string>> dependencies)
			{
				vector<string> sorted, work;
				set<string> candidates;

				// Helper
				auto checkDeps = [&dependencies](auto const& entry)
				{
					for (auto item : dependencies)
					{
						auto const&[dep, list] = item;
						for (auto l : list)
						{
							if (l == entry)
							{
								return true;
							}
						}
					}

					return false;
				};

				// Initial set
				for (auto const& name : resourceNames)
				{
					if (checkDeps(name))
					{
						candidates.insert(name);
					}
					else
					{
						work.push_back(name);
					}
				}

				// Process
				while (!work.empty())
				{
					auto item = work.back();
					work.pop_back();
					sorted.push_back(item);

					dependencies.erase(item);

					// Check resources for dependencies
					vector<string> toRemove;
					for (auto c : candidates)
					{
						if (!checkDeps(c))
						{
							toRemove.push_back(c);
						}
					}

					for (auto const& tr : toRemove)
					{
						work.push_back(tr);
						candidates.erase(tr);
					}
				}

				if (dependencies.size())
				{
					throw ResourceException(nullptr, "Resources have cyclic dependencies.");
				}

				return sorted;
			}

			void ResourceManager::addResourceRecord(ResourceRecord const& record)
			{
				// Get namespace map
				auto nmspIt = mNamespaces.find(record.namesp);

				if (nmspIt == mNamespaces.end())
				{
					nmspIt = mNamespaces.insert(make_pair(record.namesp, ResourceRecordMap())).first;
				}

				nmspIt->second[record.baseData.name] = record;
			}

			void ResourceManager::instantiateAllResources(bool create, bool load, ResourceCallback callback, bool rootResource)
			{
				// Get all resource names and dependencies
				vector<string> resourceNames;
				map<string, vector<string>> resourceDependencies;
				map<string, ResourceRecord> recordMap;

				for (auto nItem : mNamespaces)
				{
					auto const& [namesp, recordList] = nItem;

					for (auto rItem : recordList)
					{
						auto const& [name, record] = rItem;
						
						string qualifiedName = record.namesp == ""
							? record.baseData.name
							: record.namesp + "/" + record.baseData.name;

						resourceNames.push_back(qualifiedName);

						for (auto drr : record.dependentResources)
						{
							// Parse ref to work out namespace and name
							string depNamesp, depName;
							Resource::splitName(drr.ref, record.namesp, &depNamesp, &depName);

							string depQualifiedName = depNamesp == "" 
								? depName 
								: depNamesp + "/" + depName;

							auto it = resourceDependencies.insert(make_pair(depQualifiedName, vector<string>()));
							it.first->second.push_back(qualifiedName);
						}

						recordMap[qualifiedName] = record;
					}
				}

				// Order all resources by dependencies, so we can be certain that all
				// dependent resources are instantiated before they are referenced.
				auto sortedResources = sortResourcesByDependency(resourceNames, resourceDependencies);
				
				// Instantiate
				for (auto const& resName : sortedResources)
				{
					auto const& record = recordMap[resName];
					instantiateResource(record, create, load, callback, rootResource);
				}
			}

			ResourcePtr ResourceManager::instantiateResource(ResourceRecord const& record, bool create, bool load, ResourceCallback callback, bool rootResource)
			{
				// Find factory
				auto facIt = mResourceFactories.find(record.baseData.type);
				if (facIt == mResourceFactories.end())
				{
					throw ResourceSystemException(format("Could not create resource '{}' because no factory for resource of type '{}' is registered.", 
						record.baseData.name, record.baseData.type));
				}

				// Create
				string source = record.isComposite ? "" : (record.baseData.locationFound ? record.baseData.location : "");
				auto resource = facIt->second->createResource(record.baseData.name, record.namesp, source, record.baseData.tags, record.resourceLocation);
				auto resPtr = ResourcePtr(resource);

				mwLogger->info("Instantiated resource: " + resPtr->getQualifiedName());

				// Add dependent resources
				for (auto const& depResource: record.dependentResources)
				{
					// Get namespace for dependent resource
					string depNamesp, depResName;
					Resource::splitName(depResource.ref, record.namesp, &depNamesp, &depResName);

					auto acquiredRes = acquireResource(depResName, depNamesp);
					if (depResource.id != "")
					{
						resource->addDependentResource(depResource.id, acquiredRes);
					}
					else
					{
						resource->addDependentResource(acquiredRes);
					}
				}

				// Set definitions
				for (auto const& def : record.definitions)
				{
					resource->addDefinition(def.first, def.second);
				}

				if (callback)
				{
					callback(resPtr, ResourceState::Instantiated, rootResource);
				}

				if (create)
				{
					createResource(ResourcePtr(resource), callback, rootResource);
				}

				// Load
				if (load)
				{
					loadResource(ResourcePtr(resource), callback, rootResource);
				}

				// Add to lookup
				auto namespIt = mResources.find(record.namesp);
				if (namespIt == mResources.end())
				{
					mResources[record.namesp] = ResourceMap();
				}

				mResources[record.namesp][record.baseData.name] = resPtr;
				return resPtr;
			}

			void ResourceManager::addResourceFactory(ResourceFactory* factory)
			{
				auto const& type = factory->getType();
				auto it = mResourceFactories.find(type);

				if (it != mResourceFactories.end())
				{
					throw ResourceSystemException(format("Factory for resource of type '{}' is already registered.", type));
				}

				mResourceFactories[type] = factory;
			}

			void ResourceManager::addResourceLocationFactory(string const& type, ResourceLocationFactory factory)
			{
				auto it = mLocationFactories.find(type);

				if (it != mLocationFactories.end())
				{
					throw ResourceSystemException(format("Factory for resource location of type '{}' is already registered.", type));
				}

				mLocationFactories[type] = factory;
			}

			void ResourceManager::addResourceDefinitionFactory(ResourceDefinitionFactory* factory)
			{
				auto const& resType = factory->getResourceType();
				auto const& facType = factory->getFactoryType();

				auto it1 = Resource::msResourceDefinitionFactories.find(resType);
				if (it1 == Resource::msResourceDefinitionFactories.end())
				{
					it1 = Resource::msResourceDefinitionFactories.insert(make_pair(resType, map<string, ResourceDefinitionFactory*>())).first;
				}

				auto& innerMap = it1->second;
				auto it2 = innerMap.find(facType);
				if (it2 != innerMap.end())
				{
					throw ResourceSystemException(format("DefinitionFactory for resource type '{}', factory type '{}' is already registered.",
						resType, facType));
				}

				innerMap[facType] = factory;
			}

			void ResourceManager::addResourceLocation(string const& type, string const& location, string const& definitionFile)
			{
				auto it = mLocationFactories.find(type);

				if (it == mLocationFactories.end())
				{
					throw ResourceSystemException(format("No factory for resource location of type '{}' is registered.", type));
				}

				ResourceLocationRecord record;
				record.location = it->second(location, definitionFile);
				record.scanned = false;

				mLocations.push_back(record);
			}

			void ResourceManager::addResources(string const& file)
			{
				WP_UNUSED(file);

				NOT_IMPLEMENTED_YET("adding resources from a file");
			}

			void ResourceManager::scanLocations(ResourceLocationCallback callback)
			{
				for (auto record: mLocations)
				{
					if (record.scanned)
					{
						continue;
					}

					mwLogger->info("Scanning resource location: " + record.location->getName());


					if (callback)
					{
						callback(record.location->getName(), ResourceLocationState::Unscanned);
					}

					record.location->scan();

					// Add resource records to master list
					auto const& namespaceRecords = record.location->getNamespaceRecords();
					for (auto nmspIt: namespaceRecords)
					{
						auto const& namesp = nmspIt.second;
						for (auto const& recordIt: namesp.resourceRecords)
						{
							addResourceRecord(recordIt.second);
						}
					}

					record.scanned = true;

					if (callback)
					{
						callback(record.location->getName(), ResourceLocationState::Scanned);
					}
				}

				// Verify once all namespaces are scanned
				for (auto record : mLocations)
				{
					mwLogger->info("Verifying resource location: " + record.location->getName());
					record.location->validateResourceDefinitions();
				}

				instantiateAllResources(false, false);
			}

			ResourcePtr ResourceManager::getResource(string const& name, string const& namesp)
			{
				auto namespIt = mResources.find(namesp);

				if (namespIt == mResources.end())
				{
					throw ResourceSystemException(format("Namespace '{}' could not be found.", namesp));
				}

				auto it = namespIt->second.find(name);

				if (it == namespIt->second.end())
				{
					if (namesp.empty())
					{
						throw ResourceSystemException(format("Resource '{}' could not be found.", name));
					}
					else
					{
						throw ResourceSystemException(format("Resource '{}/{}' could not be found.", namesp, name));
					}
				}

				return it->second;
			}

			ResourcePtr ResourceManager::getQualifiedResource(std::string const& name)
			{
				auto resParts = StringUtils::split(name, "/");
				auto numParts = resParts.size();

				if (numParts > 2)
				{
					throw ResourceSystemException(format("Resource '{}' is not a valid qualified name.", name));
				}

				auto resNamesp = numParts == 1 ? "" : resParts[0];
				auto resName = resParts[numParts - 1];

				return getResource(resName, resNamesp);
			}

			vector<ResourcePtr> ResourceManager::getResourcesByType(string const& type)
			{
				vector<ResourcePtr> resources;

				for (auto kvp: mResources)
				{
					for (auto const& res: kvp.second)
					{
						if (res.second->getType() == type)
						{
							resources.push_back(res.second);
						}
					}
				}

				return resources;
			}

			vector<ResourcePtr> ResourceManager::getNamespaceResources(string const& namesp)
			{
				vector<ResourcePtr> resources;

				auto namespIt = mResources.find(namesp);

				if (namespIt == mResources.end())
				{
					throw ResourceSystemException(format("Namespace '{}' could not be found.", namesp));
				}

				for (auto kvp : namespIt->second)
				{
					resources.push_back(kvp.second);
				}

				return resources;
			}

			vector<ResourcePtr> ResourceManager::getAllResources()
			{
				vector<ResourcePtr> resources;

				for (auto kvp : mResources)
				{
					for (auto const& res : kvp.second)
					{
						resources.push_back(res.second);
					}
				}

				return resources;
			}

			ResourcePtr ResourceManager::acquireResource(string const& name, string const& namesp)
			{
				auto res = getResource(name, namesp);
				acquireResource(res);
				return res;
			}

			void ResourceManager::acquireResource(ResourcePtr resource)
			{
				resource->mRefCount++;
			}

			void ResourceManager::releaseResource(ResourcePtr resource, ResourceCallback callback, bool rootResource)
			{			
				// If it's already unloaded, we still want to fire the callback
				if (callback)
				{
					callback(resource, ResourceState::Releasing, rootResource);

					if (!resource->mLoaded)
					{
						if (resource->mCreated)
						{
							callback(resource, ResourceState::Created, rootResource);
						}
						else
						{
							callback(resource, ResourceState::Instantiated, rootResource);
						}

						callback(resource, ResourceState::Released, rootResource);
						return;
					}
				}

				// Handle the case where a resource has been loaded but nothing has actually needed
				// to acquire it.
				bool loadedButNeverAcquired = resource->mLoaded && resource->mRefCount == 0;
				bool readyForUnload = resource->mRefCount == 1;

				if (resource->mRefCount > 0)
				{
					resource->mRefCount--;
				}

				if (loadedButNeverAcquired || readyForUnload)
				{
					// Children
					for (auto childRes : resource->mDependentResourceList)
					{
						releaseResource(childRes, callback, false);
					}

					_unloadResource(resource, callback, rootResource);
				}

				if (callback)
				{
					callback(resource, ResourceState::Released, rootResource);
				}
			}

			bool ResourceManager::isResourceCreated(ResourcePtr resource) const
			{
				return resource->mCreated;
			}

			bool ResourceManager::isResourceLoaded(ResourcePtr resource) const
			{
				return resource->mLoaded;
			}

			void ResourceManager::_createResource(ResourcePtr resource, ResourceCallback callback, bool rootResource)
			{
				if (resource->mCreated)
				{
					return;
				}

				if (callback)
				{
					callback(resource, ResourceState::Creating, rootResource);
				}

				mwLogger->info("Creating resource: " + resource->getQualifiedName());

				DataStreamPtr dataPtr;
				if (resource->mSource != "")
				{
					dataPtr = make_shared<DataStream>(resource->mwLocation, resource->getSource(), resource->getNamespace());
					dataPtr->read();
				}

				resource->create(dataPtr, this);
				resource->mCreated = true;

				if (callback)
				{
					callback(resource, ResourceState::Created, rootResource);
				}
			}

			void ResourceManager::createResource(ResourcePtr resource, ResourceCallback callback, bool rootResource)
			{
				// Create dependent resources
				for (auto res: resource->mDependentResourceList)
				{
					createResource(res, callback, false);
				}

				_createResource(resource, callback, rootResource);
			}

			void ResourceManager::_destroyResource(ResourcePtr resource, ResourceCallback callback, bool rootResource)
			{
				if (resource->mLoaded)
				{
					throw ResourceException(resource.get(), "Resource cannot be destroyed before it has been unloaded.");
				}

				if (resource->mCreated)
				{
					if (callback)
					{
						callback(resource, ResourceState::Destroying, rootResource);
					}

					mwLogger->info("Destroying resource: " + resource->getQualifiedName());

					resource->destroy();
					resource->mCreated = false;

					if (callback)
					{
						callback(resource, ResourceState::Instantiated, rootResource);
					}
				}
			}

			void ResourceManager::_loadResource(ResourcePtr resource, ResourceCallback callback, bool rootResource)
			{
				if (!resource->mCreated)
				{
					throw ResourceException(resource.get(), "Resource cannot be loaded before it has been created.");
				}

				if (resource->mLoaded)
				{
					return;
				}

				if (callback)
				{
					callback(resource, ResourceState::Loading, rootResource);
				}

				mwLogger->info("Loading resource: " + resource->getQualifiedName());

				if (resource->load(mwRenderSystem, mwRenderResourceMgr))
				{
					resource->mLoaded = true;
					if (callback)
					{
						callback(resource, ResourceState::Loaded, rootResource);
					}
				}
			}

			void ResourceManager::loadResource(ResourcePtr resource, ResourceCallback callback, bool rootResource)
			{
				// Load dependent resources
				for (auto res : resource->mDependentResourceList)
				{
					loadResource(res, callback, false);
				}

				_loadResource(resource, callback, rootResource);
			}

			void ResourceManager::_unloadResource(ResourcePtr resource, ResourceCallback callback, bool rootResource)
			{
				if (resource->mLoaded)
				{
					if (callback)
					{
						callback(resource, ResourceState::Unloading, rootResource);
					}

					mwLogger->info("Unloading resource: " + resource->getQualifiedName());

					if (resource->unload(mwRenderSystem, mwRenderResourceMgr))
					{
						resource->mLoaded = false;
						if (callback)
						{
							callback(resource, ResourceState::Created, rootResource);
						}
					}
				}
			}

			void ResourceManager::createNamespaceResources(string const& namesp, ResourceCallback callback)
			{
				auto it = mResources.find(namesp);
				if (it != mResources.end())
				{
					auto const& resourceRecord = it->second;
					for (auto const& resource : resourceRecord)
					{
						createResource(resource.second, callback, true);
					}
				}
				else
				{
					throw ResourceSystemException(format("Namespace '{}' could not be found.", namesp));
				}
			}

			void ResourceManager::createAllResources(ResourceCallback callback)
			{
				for (auto const& it : mResources)
				{
					createNamespaceResources(it.first, callback);
				}
			}

			void ResourceManager::createResources(vector<ResourcePtr> const& resources, ResourceCallback callback)
			{
				for (auto resource : resources)
				{
					createResource(resource, callback, true);
				}
			}

			void ResourceManager::destroyNamespaceResources(string const& namesp, ResourceCallback callback)
			{
				auto it = mResources.find(namesp);
				if (it != mResources.end())
				{
					auto const& resourceRecord = it->second;
					for (auto const& resource : resourceRecord)
					{
						_destroyResource(resource.second, callback, true);
					}
				}
				else
				{
					throw ResourceSystemException(format("Namespace '{}' could not be found.", namesp));
				}
			}

			void ResourceManager::destroyAllResources(ResourceCallback callback)
			{
				for (auto const& it : mResources)
				{
					destroyNamespaceResources(it.first, callback);
				}
			}

			void ResourceManager::destroyResources(vector<ResourcePtr> const& resources, ResourceCallback callback)
			{
				for (auto resource : resources)
				{
					_destroyResource(resource, callback, true);
				}
			}

			void ResourceManager::loadNamespaceResources(string const& namesp, bool createFirst, ResourceCallback callback)
			{
				auto it = mResources.find(namesp);
				if (it != mResources.end())
				{
					auto const& resourceRecord = it->second;
					for (auto const& resource : resourceRecord)
					{
						if (!resource.second->mCreated && createFirst)
						{
							createResource(resource.second, callback, true);
						}

						loadResource(resource.second, callback, true);
					}
				}
				else
				{
					throw ResourceSystemException(format("Namespace '{}' could not be found.", namesp));
				}
			}

			void ResourceManager::loadAllResources(bool createFirst, ResourceCallback callback)
			{
				for (auto const& it : mResources)
				{
					loadNamespaceResources(it.first, createFirst, callback);
				}
			}

			void ResourceManager::loadResources(vector<ResourcePtr> const& resources, bool createFirst, ResourceCallback callback)
			{
				for (auto resource : resources)
				{
					if (createFirst)
					{
						createResource(resource, callback, true);
					}

					loadResource(resource, callback, true);
				}
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE