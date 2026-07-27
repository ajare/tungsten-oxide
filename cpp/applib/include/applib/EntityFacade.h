#pragma once

#include <vector>

#include <willpower/application/resourcesystem/Resource.h>

#include "Platform.h"
#include "EntityHandler.h"

namespace applib
{

	class APPLIB_API EntityFacade
	{
	protected:

		std::vector<Entity> mEntities;

		std::vector<Entity> mCreatedEntities;

		uint32_t mCount;

	private:

		void resize(size_t newSize);

		void autoResize(size_t newSize);

		bool freeCapacity() const;

		void addCreatedEntities();

		void updateAllEntities(bool controlActive, float frameTime);

	protected:

		virtual void createEntityRenderer(
			mpp::ScenePtr scene,
			mpp::RenderSystem* renderSystem,
			mpp::ResourceManager* renderResourceMgr,
			int renderOrder) = 0;
	public:

		explicit EntityFacade(size_t initialSize);

		virtual ~EntityFacade() = default;

		void initialise(
			mpp::ScenePtr scene, 
			mpp::RenderSystem* renderSystem, 
			mpp::ResourceManager* renderResourceMgr, 
			int renderOrder);

		uint32_t getCount() const;

		Entity const& getEntity(uint32_t index) const;

		Entity const& getEntityById(uint32_t id) const;

		void createEntity(int type, wp::Vector2 const& position, float angle, bool addImmediately);

		virtual void updateRenderer(float frameTime) = 0;

		void updateEntities(bool controlActive, float frameTime);

		virtual void setRenderersVisible(bool visible) = 0;
	};

} // applib
