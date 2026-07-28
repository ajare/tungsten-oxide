#pragma once

#include <string>

#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"

#include "Platform.h"
#include "TrackMaterial.h"

namespace applib
{

	class APPLIB_API TrackMaterialResourceDefinitionFactory : public wp::application::resourcesystem::ResourceDefinitionFactory
	{
	public:

		explicit TrackMaterialResourceDefinitionFactory(std::string const& factoryType);
	};

} // applib
