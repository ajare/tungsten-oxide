#pragma once

#include <string>

#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"

#include "Platform.h"
#include "Game.h"

namespace applib
{

	class APPLIB_API GameResourceDefinitionFactory : public wp::application::resourcesystem::ResourceDefinitionFactory
	{
	public:

		explicit GameResourceDefinitionFactory(std::string const& factoryType);
	};

} // applib