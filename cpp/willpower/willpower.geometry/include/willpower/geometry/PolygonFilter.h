#pragma once

#include "willpower/geometry/Filter.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class WP_GEOMETRY_API PolygonFilter : public Filter
		{
		public:

			explicit PolygonFilter(Mesh const* mesh);

			PolygonFilter& selectPolygons(IndexSet const& polygonIndices);

			PolygonFilter& selectPolygons(BoundingCircle const& bounds);

			PolygonFilter& selectPolygons(BoundingBox const& bounds);

			PolygonFilter& addPolygons(IndexSet const& polygonIndices);

			PolygonFilter& addPolygons(BoundingCircle const& bounds);

			PolygonFilter& addPolygons(BoundingBox const& bounds);
		};

	} // geometry
} // WP_NAMESPACE
