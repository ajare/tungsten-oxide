#pragma once

#include <vector>
#include <map>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include "willpower/common/Logger.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/ResourceRecord.h"
#include "willpower/application/resourcesystem/ResourceLocation.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceCallback.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/AudioSystem.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API ResourceManager
			{
				struct ResourceLocationRecord
				{
					ResourceLocation* location;
					bool scanned;
				};

			public:

				typedef std::map<std::string, ResourceRecord> ResourceRecordMap;
				typedef ResourceRecordMap::const_iterator ResourceRecordIterator;

			private:

				Logger* mwLogger;

				// Render system for graphics resources
				mpp::ResourceManager* mwRenderResourceMgr;

				mpp::RenderSystem* mwRenderSystem;

				// Audio system
				AudioSystem* mwAudioSystem;

				// Resource locations
				std::map<std::string, ResourceLocationFactory> mLocationFactories;

				std::vector<ResourceLocationRecord> mLocations;

				// Resource records
				std::map<std::string, ResourceRecordMap> mNamespaces;

				// Resources
				std::map<std::string, ResourceFactory*> mResourceFactories;

				typedef std::map<std::string, ResourcePtr> ResourceMap;
				std::map<std::string, ResourceMap> mResources;

			private:

				void addResourceRecord(ResourceRecord const& record);

				void instantiateAllResources(bool create, bool load, ResourceCallback callback = nullptr, bool rootResource = true);

				ResourcePtr instantiateResource(ResourceRecord const& record, bool create = false, bool load = false, ResourceCallback callback = nullptr, bool rootResource = true);

				void _createResource(ResourcePtr resource, ResourceCallback callback = nullptr, bool rootResource = true);

				void _destroyResource(ResourcePtr resource, ResourceCallback callback = nullptr, bool rootResource = true);

				void _loadResource(ResourcePtr resource, ResourceCallback callback = nullptr, bool rootResource = true);

				void _unloadResource(ResourcePtr resource, ResourceCallback callback = nullptr, bool rootResource = true);

				static std::vector<std::string> sortResourcesByDependency(std::vector<std::string> const& resourceNames, std::map<std::string, std::vector<std::string>> dependencies);

			public:

				ResourceManager(mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, AudioSystem* audioSystem, Logger* logger);

				virtual ~ResourceManager();

				void addResourceFactory(ResourceFactory* factory);

				void addResourceLocationFactory(std::string const& type, ResourceLocationFactory factory);

				void addResourceDefinitionFactory(ResourceDefinitionFactory* factory);

				void addResourceLocation(std::string const& type, std::string const& location, std::string const& definitionFile);

				void addResources(std::string const& file);

				void scanLocations(ResourceLocationCallback callback = nullptr);

				ResourcePtr getResource(std::string const& name, std::string const& namesp = "");

				ResourcePtr getQualifiedResource(std::string const& name);

				std::vector<ResourcePtr> getResourcesByType(std::string const& type);

				std::vector<ResourcePtr> getNamespaceResources(std::string const& namesp);

				std::vector<ResourcePtr> getAllResources();

				ResourceDefinitionFactory* getResourceDefinitionFactory(std::string const& resType, std::string const& facType, bool errorIfNotFound = true);

				ResourcePtr acquireResource(std::string const& name, std::string const& namesp = "");

				void acquireResource(ResourcePtr resource);

				void releaseResource(ResourcePtr resource, ResourceCallback callback = nullptr, bool rootResource = true);

				bool isResourceCreated(ResourcePtr resource) const;

				bool isResourceLoaded(ResourcePtr resource) const;

				void createResource(ResourcePtr resource, ResourceCallback callback = nullptr, bool rootResource = true);

				void loadResource(ResourcePtr resource, ResourceCallback callback = nullptr, bool rootResource = true);

				void createNamespaceResources(std::string const& namesp, ResourceCallback callback = nullptr);
				
				void createAllResources(ResourceCallback callback = nullptr);

				void createResources(std::vector<ResourcePtr> const& resources, ResourceCallback callback = nullptr);

				void destroyNamespaceResources(std::string const& namesp, ResourceCallback callback = nullptr);

				void destroyAllResources(ResourceCallback callback = nullptr);

				void destroyResources(std::vector<ResourcePtr> const& resources, ResourceCallback callback = nullptr);

				void loadNamespaceResources(std::string const& namesp, bool createFirst, ResourceCallback callback = nullptr);

				void loadAllResources(bool createFirst, ResourceCallback callback = nullptr);

				void loadResources(std::vector<ResourcePtr> const& resources, bool createFirst, ResourceCallback callback = nullptr);

			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

