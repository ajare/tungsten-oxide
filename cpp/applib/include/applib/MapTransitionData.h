#pragma once

#include <memory>

#include <willpower/application/resourcesystem/Resource.h>

namespace applib
{

	struct MapTransitionData
	{
		struct MapData
		{
			std::shared_ptr<wp::application::resourcesystem::Resource> map;
		};

		MapData prevMap, nextMap;

		std::string nextMapName;
	};

}  // applib