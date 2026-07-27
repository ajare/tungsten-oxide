#pragma once

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/ProgramResourceDefinitionFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			class WP_APPLICATION_API ProgramDefaultDefinitionFactory : public ProgramResourceDefinitionFactory
			{
			public:

				ProgramDefaultDefinitionFactory();

				void create(Resource* resource, ResourceManager* resourceMgr, wp::XmlNode* node) override;
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

