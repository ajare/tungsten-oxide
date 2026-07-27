#pragma once

#include <applib/Model.h>

#include "Platform.h"


struct BooleanWorldModel : public applib::Model
{
	BooleanWorldModel(applib::EntityHandlerFactoryFunction handlerFactory, wp::application::resourcesystem::ResourceManager* resourceMgr)
		: applib::Model(handlerFactory, resourceMgr)
	{
	}
};
