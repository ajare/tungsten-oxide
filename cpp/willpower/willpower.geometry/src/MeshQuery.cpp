#include "willpower/common/MathsUtils.h"

#include "willpower/geometry/MeshQuery.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace std;
		using namespace WP_NAMESPACE;

		MeshQuery::MeshQuery(Mesh const* mesh)
			: mwMesh(mesh)
		{
		}

		IndexSet MeshQuery::getEdgesIntersectingLine(Vector2 const& v0, Vector2 const& v1)
		{
			IndexSet edgeIndices, candidateIndices = getEdgeIndicesInBoundingObject(BoundingBox(v0, v1 - v0));
			
			for (uint32_t candidate: candidateIndices)
			{
				auto const& edge = mwMesh->getEdge(candidate);
				auto const& edgev0 = mwMesh->getVertex(edge.getFirstVertex()).getPosition();
				auto const& edgev1 = mwMesh->getVertex(edge.getSecondVertex()).getPosition();

				//if (MathsUtils::lineIntersectsLine(v0, v1, edgev0, edgev1) != MathsUtils::LineIntersectionType::NotIntersecting)
				if (MathsUtils::lineIntersectsLine(v0, v1, edgev0, edgev1) == MathsUtils::LineIntersectionType::Intersecting)
				{
					edgeIndices.insert(candidate);
				}
			}

			return edgeIndices;
		}

		int32_t MeshQuery::getPolygonContainingPoint(float x, float y)
		{
			uint32_t polygonIndex = mwMesh->getFirstPolygonIndex();
			while (!mwMesh->polygonIndexIterationFinished(polygonIndex))
			{
				auto const& polygon = mwMesh->getPolygon(polygonIndex);
				
				if (polygon.pointInside(x, y))
				{
					return polygonIndex;
				}

				polygonIndex = mwMesh->getNextPolygonIndex(polygonIndex);
			}

			return -1;
		}

	} // geometry
} // WP_NAMESPACE
