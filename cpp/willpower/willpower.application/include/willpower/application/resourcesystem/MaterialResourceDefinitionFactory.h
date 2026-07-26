#pragma once

#include "willpower/common/XmlReader.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/MaterialResource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API MaterialResourceDefinitionFactory : public ResourceDefinitionFactory
			{
			protected:

				void addResourceTextureDefinition(MaterialResource* resource, std::string const& sampler, std::string const& resName);

				void addDefaultTextureDefinition(MaterialResource* resource, std::string const& sampler);

			public:

				explicit MaterialResourceDefinitionFactory(std::string const& factoryType);
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

#pragma once
