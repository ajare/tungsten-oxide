#pragma once

#include <willpower/application/resourcesystem/Resource.h>

#include "MapTransitionData.h"

namespace applib
{

	struct StateTransitionData
	{
		std::string prevStateName;
		MapTransitionData mapData;
		wp::application::resourcesystem::ResourcePtr gameResource;
		void* userData{ nullptr };
	};

} // applib