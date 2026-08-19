#pragma once

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>

#include "Platform.h"

namespace applib
{

	struct PhysicalStats
	{
		wp::Vector2 position;
		float floorZ;
		float angle;
		float pitch;
		bool collides;
		wp::BoundingBox bounds;
	};

} // applib