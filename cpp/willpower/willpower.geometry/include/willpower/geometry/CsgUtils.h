#pragma once

#include <vector>

#include "willpower/common/Vector2.h"

#include "willpower/geometry/Platform.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{
		class WP_GEOMETRY_API CsgUtils
		{
		public:

			typedef std::vector<wp::Vector2> Polygon;

		public:

			static std::vector<Polygon> opUnion(std::vector<Polygon> const& polygons);
		};

	} // geometry
} // WP_NAMESPACE
