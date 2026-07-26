#pragma once

#include <string>

#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/AudioBankResource.h"


namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API AudioBankResourceDefinitionFactory : public wp::application::resourcesystem::ResourceDefinitionFactory
			{
			public:

				explicit AudioBankResourceDefinitionFactory(std::string const& factoryType);
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE
