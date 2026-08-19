#pragma once

#include <vector>
#include <map>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include <willpower/application/resourcesystem/Resource.h>

#include "Entity.h"
#include "EntityHandler.h"
#include "EntityFacade.h"
#include "EntityFacadeFactory.h"

namespace applib
{

	class APPLIB_API EntityManager
	{
		std::vector<EntityFacade*> mFacades;

		std::vector<EntityFacade*> mTypeFacadeMapping;

		std::map<std::string, EntityFacadeFactory*> mFacadeFactories;

		mpp::RenderSystem* mwRenderSystem;

		mpp::ResourceManager* mwRenderResourceMgr;

	public:

		EntityManager(
			mpp::RenderSystem* renderSystem,
			mpp::ResourceManager* renderResourceMgr);

		virtual ~EntityManager();

		void registerFacadeFactory(std::string const& type, EntityFacadeFactory* factory);

		void createFacade(
			std::string const& facadeType,
			mpp::ScenePtr scene,
			std::vector<int> const& types, 
			int renderOrder,
			size_t initialSize);

		Entity const& getPlayerEntity() const;

		void createEntity(int type, wp::Vector2 const& position, float angle, bool addImmediately);

		void updateEntities(bool controlActive, float frameTime);

		void updateRenderers(float frameTime);

		void setRenderersVisible(bool visible);
	};

} // applib