#pragma once

#include <memory>

#include <willpower/application/resourcesystem/ResourceManager.h>

#include "Platform.h"
#include "EntityHandler.h"


namespace applib
{

	typedef std::function<EntityHandler*()> EntityHandlerFactoryFunction;

	struct Model
	{
		std::shared_ptr<EntityHandler> entityHandler;

	public:

		Model(EntityHandlerFactoryFunction handlerFactory, wp::application::resourcesystem::ResourceManager* resourceMgr)
		{
			auto entityHandlerPtr = handlerFactory();
			entityHandler = std::shared_ptr<EntityHandler>(entityHandlerPtr);
		}

		virtual ~Model() = default;
	};

} // applib