#pragma once

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/ImageResourceDefinitionFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			class WP_APPLICATION_API ImageDefaultDefinitionFactory : public ImageResourceDefinitionFactory
			{
			public:

				ImageDefaultDefinitionFactory();

				void create(Resource* resource, ResourceManager* resourceMgr, XmlNode* node) override;
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

