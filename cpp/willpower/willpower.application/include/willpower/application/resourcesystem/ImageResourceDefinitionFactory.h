#pragma once


#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API ImageResourceDefinitionFactory : public ResourceDefinitionFactory
			{
			public:

				explicit ImageResourceDefinitionFactory(std::string const& factoryType);
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

