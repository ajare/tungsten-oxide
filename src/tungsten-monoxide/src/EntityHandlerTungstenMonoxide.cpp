#include <format>

#include <willpower/common/Vector2.h>

#include "EntityHandlerTungstenMonoxide.h"
#include "EntityType.h"

#include "GameException.h"
#include "EntityProperties.h"

using namespace std;
using namespace wp;
using namespace applib;


map<EntityType, string> gEntityTypeNames = {
	{EntityType::Player, "Player"}
};

EntityHandlerTungstenMonoxide::EntityHandlerTungstenMonoxide()
	: EntityHandler()
	, mInputEnabled(true)
{
}

void EntityHandlerTungstenMonoxide::enableInput(bool enable)
{
	mInputEnabled = enable;
}

bool EntityHandlerTungstenMonoxide::isInputEnabled() const
{
	return mInputEnabled;
}

string EntityHandlerTungstenMonoxide::getPrototypeName(int type)
{
	auto it = gEntityTypeNames.find((EntityType)type);

	if (it != gEntityTypeNames.end())
	{
		return it->second;
	}
	else
	{
		throw GameException(std::format("Unknown entity type: {}", type));
	}
}

void EntityHandlerTungstenMonoxide::setupImpl(Entity* entity)
{
	VAR_UNUSED(entity);
}

void EntityHandlerTungstenMonoxide::destroyImpl(Entity *entity)
{
	VAR_UNUSED(entity);
}

bool EntityHandlerTungstenMonoxide::updateImpl(Entity *entity, bool inputControlled, float frameTime)
{
	if (inputControlled)
	{
		if (mInputEnabled)
		{
		}
	}
	else
	{
		auto entityType = entity->getType();

		switch (entityType)
		{
		case (int)EntityType::Player:
			break;

		default:
			throw GameException(std::format("Unhandled entity type: {}", (int)entityType));
		}
	}

	return true;
}

void EntityHandlerTungstenMonoxide::updateVisual(Entity* entity, float frameTime)
{
}

void EntityHandlerTungstenMonoxide::peekInput(applib::Entity const& entity, wp::Vector2* curPosition, wp::Vector2* newPosition, float* curAngle, float* newAngle, float* curPitch, float *newPitch, wp::Vector2* velocity, float frameTime) const
{
}

bool EntityHandlerTungstenMonoxide::update(Entity* entity, bool controlActive, float frameTime)
{
	// Update logic
	bool inputControlled = controlActive && (entity->getId() == 0);
	bool alive = updateImpl(entity, inputControlled, frameTime);

	if (alive)
	{
	}

	return alive;
}
