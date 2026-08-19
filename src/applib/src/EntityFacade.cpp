#include <willpower/application/resourcesystem/AnimationSetResource.h>
#include <willpower/application/resourcesystem/ImageSetResource.h>

#include "EntityFacade.h"
#include "ModelInstance.h"
#include "Exceptions.h"

namespace applib
{
	using namespace std;
	using namespace wp;

	EntityFacade::EntityFacade(size_t initialSize)
		: mCount(0)
	{
		resize(initialSize);
	}

	void EntityFacade::initialise(mpp::ScenePtr scene, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, int renderOrder)
	{
		createEntityRenderer(scene, renderSystem, renderResourceMgr, renderOrder);
	}

	uint32_t EntityFacade::getCount() const
	{
		return mCount;
	}

	Entity const& EntityFacade::getEntity(uint32_t index) const
	{
		return mEntities[index];
	}

	Entity const& EntityFacade::getEntityById(uint32_t id) const
	{
		for (auto const& entity : mEntities)
		{
			if (entity.getId() == id)
			{
				return entity;
			}
		}

		throw Exception("Could not get entity " + id);
	}

	void EntityFacade::resize(size_t newSize)
	{
		auto curSize = mEntities.size();
		if (newSize > curSize)
		{
			mEntities.resize(newSize);
		}
	}

	void EntityFacade::autoResize(size_t newSize)
	{
		if (newSize > mEntities.size())
		{
			auto tgtSize = mEntities.size();
			while (tgtSize < newSize)
			{
				tgtSize *= 3;
				tgtSize /= 2;
				tgtSize += 8;
			}

			resize(tgtSize);
		}
	}

	bool EntityFacade::freeCapacity() const
	{
		return mCount < mEntities.size();
	}

	void EntityFacade::addCreatedEntities()
	{
		// Added created entities
		autoResize(mCount + mCreatedEntities.size());

		for (auto const& entity : mCreatedEntities)
		{
			mEntities[mCount++] = entity;
		}

		mCreatedEntities.clear();
	}

	void EntityFacade::createEntity(int type, Vector2 const& position, float angle, bool addImmediately)
	{
		Entity entity;

		ModelInstance::entityHandler()->setup(&entity, type, position, angle);

		if (addImmediately)
		{
			autoResize(mCount + 1);
			mEntities[mCount++] = entity;
		}
		else
		{
			mCreatedEntities.push_back(entity);
		}
	}

	void EntityFacade::updateAllEntities(bool controlActive, float frameTime)
	{
		unsigned int tCount{ 0 };
		for (unsigned int i = 0; i < mCount; ++i)
		{
			auto* entity = &mEntities[i];

			if (ModelInstance::entityHandler()->update(entity, controlActive, frameTime))
			{
				if (i != tCount)
				{
					mEntities[tCount] = mEntities[i];
				}

				tCount++;
			}
			else
			{
				ModelInstance::entityHandler()->destroy(entity);
			}
		}

		mCount = tCount;
	}

	void EntityFacade::updateEntities(bool controlActive, float frameTime)
	{
		addCreatedEntities();
		updateAllEntities(controlActive, frameTime);
	}

} // applib