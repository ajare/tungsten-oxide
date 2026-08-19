#include <cassert>
#include <format>

#include "EntityHandler.h"
#include "Exceptions.h"

namespace applib
{
	using namespace std;
	using namespace wp;

	EntityHandler::EntityHandler()
		: mMouseScreenX(-1.0f)
		, mMouseScreenY(-1.0f)
		, mMouseDeltaX(0.0f)
		, mMouseDeltaY(0.0f)
	{
	}

	void EntityHandler::getMouseScreenPosition(float* mouseX, float* mouseY) const
	{
		*mouseX = mMouseScreenX;
		*mouseY = mMouseScreenY;
	}

	void EntityHandler::copyEntityComponents(entt::entity from, entt::entity to)
	{
		for (auto [id, storage] : mComponentRegistry.storage())
		{
			if (storage.contains(from))
			{
				storage.push(to, storage.value(from));
			}
		}
	}

	entt::entity EntityHandler::registerPrototype(string const& protoName)
	{
		if (mProtoIds.find(protoName) != mProtoIds.end())
		{
			throw Exception(std::format("Entity prototype '{}' already registered in EntityHandler.", protoName));
		}

		auto id = mComponentRegistry.create();
		mProtoIds[protoName] = id;
		return id;
	}

	void EntityHandler::setActiveInputStates(vector<string> const& states, float mouseScreenX, float mouseScreenY, float mouseDeltaX, float mouseDeltaY)
	{
		mActiveInputStates = states;
		mMouseScreenX = mouseScreenX;
		mMouseScreenY = mouseScreenY;
		mMouseDeltaX = mouseDeltaX;
		mMouseDeltaY = mouseDeltaY;
	}

	vector<string> const& EntityHandler::getActiveInputStates() const
	{
		return mActiveInputStates;
	}

	void EntityHandler::setup(Entity* entity, int type, wp::Vector2 const& position, float angle)
	{
		entity->setup(type);

		// Set up components
		auto prototypeName = getPrototypeName(type);

		auto protoIt = mProtoIds.find(prototypeName);
		if (protoIt == mProtoIds.end())
		{
			throw Exception(std::format("Entity prototype '{}' not registered in EntityHandler", prototypeName));
		}

		auto protoId = protoIt->second;
		entity->mCompSysId = mComponentRegistry.create();

		for (auto [id, storage] : mComponentRegistry.storage()) 
		{
			storage.push(entity->mCompSysId, storage.value(protoId));
		}

		if (entityHasComponent<PhysicalStats>(*entity))
		{
			auto& physicalStats = getEntityComponent<PhysicalStats>(*entity);

			physicalStats.position = position;
			physicalStats.angle = angle;
			physicalStats.pitch = 0.0f;
			physicalStats.bounds.setPosition(position - physicalStats.bounds.getSize() / 2.0f);
		}

		setupImpl(entity);
	}

	void EntityHandler::destroy(Entity *entity)
	{
		entity->destroy();

		mComponentRegistry.destroy(entity->mCompSysId);

		destroyImpl(entity);
	}

	bool EntityHandler::update(Entity* entity, bool controlActive, float frameTime)
	{
		return true;
	}

} // applib