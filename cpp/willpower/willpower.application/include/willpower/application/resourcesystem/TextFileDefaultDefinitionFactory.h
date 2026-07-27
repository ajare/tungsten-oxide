#pragma once

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/TextFileResourceDefinitionFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			class WP_APPLICATION_API TextFileDefaultDefinitionFactory : public TextFileResourceDefinitionFactory
			{
			public:

				TextFileDefaultDefinitionFactory();

				void create(Resource* resource, ResourceManager* resourceMgr, wp::XmlNode* node) override;
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

