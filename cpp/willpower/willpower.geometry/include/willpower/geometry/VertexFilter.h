#pragma once

#include "willpower/geometry/Filter.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class WP_GEOMETRY_API VertexFilter : public Filter
		{
		public:

			explicit VertexFilter(Mesh const* mesh);

			VertexFilter& selectVertices(IndexSet const& vertexIndices);

			VertexFilter& selectVertices(BoundingCircle const& bounds);

			VertexFilter& selectVertices(BoundingBox const& bounds);

			VertexFilter& selectEdgeVertices(uint32_t edgeIndex);

			VertexFilter& selectPolygonVertices(uint32_t polygonIndex);

			VertexFilter& addVertices(IndexSet const& vertexIndices);

			VertexFilter& addVertices(BoundingCircle const& bounds);

			VertexFilter& addVertices(BoundingBox const& bounds);

			VertexFilter& addEdgeVertices(uint32_t edgeIndex);

			VertexFilter& addPolygonVertices(uint32_t polygonIndex);

			VertexFilter& minimumX();

			VertexFilter& maximumX();

			VertexFilter& minimumY();

			VertexFilter& maximumY();
		};

	} // geometry
} // WP_NAMESPACE
