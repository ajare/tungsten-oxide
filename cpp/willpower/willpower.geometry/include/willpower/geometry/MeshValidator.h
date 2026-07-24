#pragma once

#include <limits>

#include "willpower/common/Vector2.h"

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Mesh.h"

#undef max

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class WP_GEOMETRY_API MeshValidator
		{
		public:

			enum Result
			{
				Valid					= 0,
				VertexInPolygon			= 1,
				EdgesCrossing			= 2,
				PolygonsIntersecting	= 4
			};

		private:

			Mesh const* mwMesh;

		private:

			uint32_t validateVertexMovePointContainment(uint32_t vertexIndex, Vector2 const& move) const;

			uint32_t validateVertexMoveEdgeCrossings(uint32_t vertexIndex, Vector2 const& move) const;

		public:

			explicit MeshValidator(Mesh const* mesh);

			uint32_t validateVertexMove(uint32_t vertexIndex, Vector2 const& move) const;

			uint32_t validatePolygonAdd(std::vector<Vector2> const& vertices) const;

		};

	} // geometry
} // WP_NAMESPACE


