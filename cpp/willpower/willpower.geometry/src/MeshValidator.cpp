#include "willpower/common/MathsUtils.h"
#include "willpower/common/BoundingBox.h"
#include "willpower/common/BoundingConvexPolygon.h"

#include "willpower/geometry/MeshValidator.h"
#include "willpower/geometry/MeshQuery.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace std;
		using namespace WP_NAMESPACE;

		MeshValidator::MeshValidator(Mesh const* mesh)
			: mwMesh(mesh)
		{
		}

		uint32_t MeshValidator::validateVertexMove(uint32_t vertexIndex, Vector2 const& move) const
		{
			uint32_t result = validateVertexMovePointContainment(vertexIndex, move);
			result |= validateVertexMoveEdgeCrossings(vertexIndex, move);

			return result;
		}
		
		uint32_t MeshValidator::validateVertexMovePointContainment(uint32_t vertexIndex, Vector2 const& move) const
		{
			// Create triangle out of the vertex position and its neighbouring vertices (it is guaranteed
			// to only be a member of two edges), and intersect against the mesh.  It should only be
			// intersecting one polygon, which is the polygon it is part of.
			vector<Vector2> trianglePoints;

			// Set up first vertex
			auto const& meshVertex = mwMesh->getVertex(vertexIndex);

			Vector2 v0 = meshVertex.getPosition() + move;

			trianglePoints.push_back(v0);

			// Get other two
			IndexVector otherIndices;
			auto const& edgeRefs = meshVertex.getEdgeReferences();
			for (uint32_t edgeRef: edgeRefs)
			{
				int32_t otherIndex = mwMesh->getEdge(edgeRef).getOtherVertex(vertexIndex);
				otherIndices.push_back((uint32_t)otherIndex);

				auto const& otherVertex = mwMesh->getVertex(otherIndex);
				trianglePoints.push_back(otherVertex.getPosition());
			}

			// Run query.
			wp::geometry::BoundingConvexPolygon bounder(wp::Vector2::ZERO, trianglePoints);
			wp::geometry::MeshQuery query(mwMesh);

			auto polygonIndices = query.getPolygonIndicesInBoundingObject(bounder);

			// If the polygons that are intersected use the other two vertices, do a point in polygon check.
			int32_t connectingEdgeIndex = mwMesh->getEdgeIndexByVertices(otherIndices[0], vertexIndex);
			auto const& polygonRefs = mwMesh->getEdge(connectingEdgeIndex).getPolygonReferences();

			// TODO: remove polys from polygonIndices if they contain polygonRef polys as holes
			// ...

			set<uint32_t> intersectedPolygonIndices;
			set_difference(
				polygonIndices.begin(), polygonIndices.end(),
				polygonRefs.begin(), polygonRefs.end(),
				inserter(intersectedPolygonIndices, intersectedPolygonIndices.end()));

			for (uint32_t polygonIndex: intersectedPolygonIndices)
			{
				auto const& polygon = mwMesh->getPolygon(polygonIndex);
				if (polygon.pointInside(v0))
				{
					return Result::VertexInPolygon;
				}
			}

			return Result::Valid;
		}

		uint32_t MeshValidator::validateVertexMoveEdgeCrossings(uint32_t vertexIndex, Vector2 const& move) const
		{
			auto const& meshVertex = mwMesh->getVertex(vertexIndex);
			Vector2 vertexPosition = meshVertex.getPosition() + move;

			MeshQuery query(mwMesh);

			// Check the edges of the vertex against others
			auto const& edgeRefs = mwMesh->getVertex(vertexIndex).getEdgeReferences();

			for (uint32_t edgeRef: edgeRefs)
			{
				// Get the moved bounding area of the edge
				auto const& vertexEdge = mwMesh->getEdge(edgeRef);
				
				int32_t otherVertexIndex = vertexEdge.getOtherVertex(vertexIndex);
				Vector2 otherVertexPosition = mwMesh->getVertex(otherVertexIndex).getPosition();

				BoundingBox bb(vertexPosition, otherVertexPosition - vertexPosition);
				
				auto const& checkEdgeIndices = query.getEdgeIndicesInBoundingObject(bb);
				for (uint32_t checkEdgeIndex : checkEdgeIndices)
				{
					auto const& checkEdge = mwMesh->getEdge(checkEdgeIndex);

					if (vertexEdge.connectedTo(checkEdge))
					{
						continue;
					}

					// Test with moved edge vertex
					Vector2 checkV1 = mwMesh->getVertex(checkEdge.getFirstVertex()).getPosition();
					Vector2 checkV2 = mwMesh->getVertex(checkEdge.getSecondVertex()).getPosition();

					if (MathsUtils::lineIntersectsLine(vertexPosition, otherVertexPosition, checkV1, checkV2) != MathsUtils::LineIntersectionType::NotIntersecting)
					{
						return Result::EdgesCrossing;
					}
				}
			}

			return Result::Valid;
		}

		uint32_t MeshValidator::validatePolygonAdd(vector<Vector2> const& vertices) const
		{
			WP_UNUSED(vertices);
			return Result::Valid;
		}

	} // geometry
} // WP_NAMESPACE
