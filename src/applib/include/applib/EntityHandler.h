#pragma once

#include <entt/entt.hpp>

#include "Platform.h"
#include "Entity.h"
#include "PhysicalStats.h"

namespace applib
{

	class APPLIB_API EntityHandler
	{
	protected:

		// Input
		std::vector<std::string> mActiveInputStates;

		float mMouseScreenX, mMouseScreenY;

		float mMouseDeltaX, mMouseDeltaY;

		// Component registries
		entt::registry mComponentRegistry;
			
		// Prototype lookups
		std::map<std::string, entt::entity> mProtoIds;

		static const size_t MaxMappings = 4096;

	private:

		virtual std::string getPrototypeName(int type) = 0;

		virtual void setupImpl(Entity* entity) = 0;

		virtual void destroyImpl(Entity* entity) = 0;

		virtual bool updateImpl(Entity* entity, bool inputControlled, float frameTime) = 0;

	public:

		EntityHandler();

		virtual ~EntityHandler() = default;

		template<typename T>
		void registerProtoComponent(entt::entity id, T const& component)
		{
			mComponentRegistry.emplace<T>(id, component);
		}

		template<typename T>
		T const& getEntityComponent(Entity const& entity) const
		{
			return mComponentRegistry.get<T>(entity.mCompSysId);
		}

		template<typename T>
		T& getEntityComponent(Entity const& entity)
		{
			return mComponentRegistry.get<T>(entity.mCompSysId);
		}

		template<typename T>
		bool entityHasComponent(Entity const& entity) const
		{
			return mComponentRegistry.try_get<T>(entity.mCompSysId) != nullptr;
		}

		void getMouseScreenPosition(float* mouseX, float* mouseY) const;

		void copyEntityComponents(entt::entity from, entt::entity to);

		entt::entity registerPrototype(std::string const& protoName);

		void setActiveInputStates(std::vector<std::string> const& states, float mouseScreenX, float mouseScreenY, float mouseDeltaX, float mouseDeltaY);

		std::vector<std::string> const& getActiveInputStates() const;

		void setup(Entity* entity, int type, wp::Vector2 const& position, float angle);

		void destroy(Entity* entity);
		
		virtual bool update(Entity* entity, bool controlActive, float frameTime);
	};

} // applib