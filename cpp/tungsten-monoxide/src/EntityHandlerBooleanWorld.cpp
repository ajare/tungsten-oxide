#include <willpower/common/Vector2.h>

#include <applib/Bullet.h>
#include <applib/BeamData.h>

#include <core/Utils.h>

#include <common/GameDefines.h>

#include "EntityHandlerBooleanWorld.h"
#include "EntityType.h"

#include "GameException.h"
#include "EntityProperties.h"

using namespace std;
using namespace wp;
using namespace applib;


map<EntityType, string> gEntityTypeNames = {
	{EntityType::Player, "Player"}
};

EntityHandlerBooleanWorld::EntityHandlerBooleanWorld(shared_ptr<AnimationDatabase> animationDatabase)
	: EntityHandler()
	, mAnimationDatabase(animationDatabase)
	, mInputEnabled(true)
{
}

void EntityHandlerBooleanWorld::enableInput(bool enable)
{
	mInputEnabled = enable;
}

bool EntityHandlerBooleanWorld::isInputEnabled() const
{
	return mInputEnabled;
}

string EntityHandlerBooleanWorld::getPrototypeName(int type)
{
	auto it = gEntityTypeNames.find((EntityType)type);

	if (it != gEntityTypeNames.end())
	{
		return it->second;
	}
	else
	{
		throw GameException(STR_FORMAT("Unknown entity type: {}", type));
	}
}

void EntityHandlerBooleanWorld::setupImpl(Entity* entity)
{
	VAR_UNUSED(entity);
}

void EntityHandlerBooleanWorld::destroyImpl(Entity *entity)
{
	VAR_UNUSED(entity);
}

bool EntityHandlerBooleanWorld::updateImpl(Entity *entity, bool inputControlled, float frameTime)
{
	if (inputControlled)
	{
		if (mInputEnabled)
		{
			wp::Vector2 curPosition, newPosition, velocity;
			float curAngle, newAngle, curPitch, newPitch;

			peekInput(*entity, &curPosition, &newPosition, &curAngle, &newAngle, &curPitch, &newPitch, &velocity, frameTime);

			mwPlayerCollider->setMovement(velocity);
			mwSimulation->update(frameTime);

			auto& physicalStats = getEntityComponent<PhysicalStats>(*entity);

			physicalStats.position = mwPlayerCollider->getCentre();
			physicalStats.angle = newAngle;
			physicalStats.pitch = newPitch;
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
			throw GameException(STR_FORMAT("Unhandled entity type: {}", (int)entityType));
		}
	}

	return true;
}

void EntityHandlerBooleanWorld::updateVisual(Entity* entity, float frameTime)
{
	auto visual = mComponentRegistry.try_get<VisualSprite>(entity->mCompSysId);
	if (!visual)
	{
		return;
	}

	auto const& anim = mAnimationDatabase->getAnimation((uint32_t)visual->animation);
	auto frame = mAnimationDatabase->getAnimationFrame((uint32_t)visual->animation, visual->frame);

	visual->timer += frameTime;
	while (visual->timer >= frame.time)
	{
		visual->timer -= frame.time;
		visual->frame += visual->direction;

		// Check if we've hit the end of the animation
		if (visual->frame == 0 || visual->frame == anim.count)
		{
			switch (anim.style)
			{
			case AnimationDatabase::LoopStyle::Forwards:
				visual->frame = 0;
				break;

			case AnimationDatabase::LoopStyle::Once:
				visual->frame = anim.count - 1;
				break;

			case AnimationDatabase::LoopStyle::PingPong:
				visual->direction *= -1;
				visual->frame += visual->direction;
				break;

			default:
				throw Exception("Unknown loop style.  Must be one of Forwards|Once|PingPoing.");
			}
		}

		// Get next frame details, in case we have skipped past the new frame entirely
		frame = mAnimationDatabase->getAnimationFrame((uint32_t)visual->animation, visual->frame);
	}
}

void EntityHandlerBooleanWorld::peekInput(applib::Entity const& entity, wp::Vector2* curPosition, wp::Vector2* newPosition, float* curAngle, float* newAngle, float* curPitch, float *newPitch, wp::Vector2* velocity, float frameTime) const
{
	auto vel = Vector2::ZERO;
	float playerSpeed = BW_PLAYER_SPEED;

	for (auto const& state : mActiveInputStates)
	{
		if (state == "Up")
		{
			vel.y += 1.0f;
		}
		else if (state == "Down")
		{
			vel.y -= 1.0f;
		}
		else if (state == "Left")
		{
			vel.x -= 1.0f;
		}
		else if (state == "Right")
		{
			vel.x += 1.0f;
		}
	}

	auto const& physicalStats = getEntityComponent<applib::PhysicalStats>(entity);

	// Get desired direction
	*curAngle = physicalStats.angle;
	*newAngle = bw::core::clamp_angle(physicalStats.angle - mMouseDeltaX);

	*curPitch = physicalStats.pitch;
	*newPitch = clamp(physicalStats.pitch + mMouseDeltaY, -85.0f, 85.0f);

	// Get desired movement
	vel.normalise();
	vel.rotateAnticlockwise(*newAngle);
	vel *= playerSpeed;

	*curPosition = physicalStats.position;
	*newPosition = *curPosition + vel * frameTime;
	*velocity = vel;
}

bool EntityHandlerBooleanWorld::update(Entity* entity, bool controlActive, float frameTime)
{
	// Update logic
	bool inputControlled = controlActive && (entity->getId() == 0);
	bool alive = updateImpl(entity, inputControlled, frameTime);

	if (alive)
	{
		// Update visual
		updateVisual(entity, frameTime);
	}

	return alive;
}
