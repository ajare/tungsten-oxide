#pragma once

#include "willpower/geometry/Filter.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class WP_GEOMETRY_API EdgeFilter : public Filter
		{
		public:

			explicit EdgeFilter(Mesh const* mesh);

			EdgeFilter& selectEdges(IndexSet const& edgeIndices);

			EdgeFilter& selectEdges(BoundingCircle const& bounds);

			EdgeFilter& selectEdges(BoundingBox const& bounds);

			EdgeFilter& selectPolygonEdges(uint32_t polygonIndex);

			EdgeFilter& addEdges(IndexSet const& vertexIndices);

			EdgeFilter& addEdges(BoundingCircle const& bounds);

			EdgeFilter& addEdges(BoundingBox const& bounds);

			EdgeFilter& addPolygonEdges(uint32_t polygonIndex);

			EdgeFilter& normalAngle(float angle, float tolerance);

			EdgeFilter& minimumCentreX();

			EdgeFilter& maximumCentreX();

			EdgeFilter& minimumCentreY();

			EdgeFilter& maximumCentreY();
		};

	} // geometry
} // WP_NAMESPACE
