#pragma once

#include "willpower/common/Vector2.h"

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Mesh.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{
		struct RegularPolygonCreationResult
		{
			uint32_t polygonIndex;
			IndexVector vertexIndices;
			IndexVector edgeIndices;
		};

		class WP_GEOMETRY_API MeshHelpers
		{
			static void generatePolygonFromVertices(Mesh* mesh, std::vector<Vector2> const& vertexPositions, RegularPolygonCreationResult* result);

		public:

			static void createRectangle(Mesh* mesh, Vector2 const& minExtent, Vector2 const& maxExtent, RegularPolygonCreationResult* result = nullptr);

			static void createRegularPolygon(Mesh* mesh, Vector2 const& centre, float radius, uint32_t sides, float angle, RegularPolygonCreationResult* result = nullptr);
		};

	} // geometry
} // WP_NAMESPACE
