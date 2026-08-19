#pragma once

#include <string>

#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"

#include "willpower/geometry/Mesh.h"

#include "Platform.h"
#include "Map.h"

namespace applib
{

	class APPLIB_API MapResourceDefinitionFactory : public wp::application::resourcesystem::ResourceDefinitionFactory
	{
	public:

		explicit MapResourceDefinitionFactory(std::string const& factoryType);

		~MapResourceDefinitionFactory();
	};

} // applib