#pragma once


#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/ShaderResource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API ShaderResourceDefinitionFactory : public ResourceDefinitionFactory
			{
			public:

				explicit ShaderResourceDefinitionFactory(std::string const& factoryType);
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

