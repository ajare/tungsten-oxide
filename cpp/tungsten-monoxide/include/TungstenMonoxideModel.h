#pragma once

#include <applib/Model.h>

#include "Platform.h"


struct TungstenMonoxideModel : public applib::Model
{
	TungstenMonoxideModel(applib::EntityHandlerFactoryFunction handlerFactory, wp::application::resourcesystem::ResourceManager* resourceMgr)
		: applib::Model(handlerFactory, resourceMgr)
	{
	}
};
