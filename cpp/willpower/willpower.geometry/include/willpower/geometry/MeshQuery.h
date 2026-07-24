#pragma once

#include <limits>

#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingBox.h"
#include "willpower/common/BoundingCircle.h"
#include "willpower/common/BoundingConvexPolygon.h"

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Mesh.h"

#undef max

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class WP_GEOMETRY_API MeshQuery
		{
			Mesh const* mwMesh;

		private:

			template<typename A>
			bool areaIntersectsEdge(A const& area, Edge const& edge) const
			{
				auto const& vertex0 = mwMesh->getVertex(edge.getFirstVertex());
				auto const& vertex1 = mwMesh->getVertex(edge.getSecondVertex());

				return area.intersectsLine(vertex0.getPosition(), vertex1.getPosition());
			}

			template<typename A>
			bool areaIntersectsPolygon(A const& area, Polygon const& polygon) const
			{
				return area.intersectsTriMesh(polygon.createBasicTriangulation());
			}

		public:

			explicit MeshQuery(Mesh const* mesh);

			//
			// Get all vertices within the specified bounding object
			//
			template<typename A>
			std::set<uint32_t> getVertexIndicesInBoundingObject(A const& area) const
			{
				std::set<uint32_t> vertexIndices;

				if (mwMesh->mVertexAccelerationGrid)
				{
					// Get candidate vertices in broadphase and then check those
					auto candidateVertices = mwMesh->mVertexAccelerationGrid->getCandidateItemsInBoundingArea(area);
					for (uint32_t candidateVertex: candidateVertices)
					{
						auto const& vertex = mwMesh->getVertex(candidateVertex);
						if (area.pointInside(vertex.getPosition()))
						{
							vertexIndices.insert(candidateVertex);
						}
					}
				}
				else
				{
					// Go through every vertex and check
					uint32_t vertexIndex = mwMesh->getFirstVertexIndex();
					while (!mwMesh->vertexIndexIterationFinished(vertexIndex))
					{
						auto const& vertex = mwMesh->getVertex(vertexIndex);
						if (area.pointInside(vertex.getPosition()))
						{
							vertexIndices.insert(vertexIndex);
						}

						vertexIndex = mwMesh->getNextVertexIndex(vertexIndex);
					}
				}

				return vertexIndices;
			}

			//
			// Gets the nearest vertex to the centre of the bounding area, which
			// is within it.
			//
			template<typename A>
			int32_t getNearestVertexIndexInBoundingObject(A const& area) const
			{
				auto candidateIndices = getVertexIndicesInBoundingObject(area);

				auto areaCentre = area.getCentre();

				float nearestDistanceSq = std::numeric_limits<float>::max();
				int32_t nearestVertexIndex = -1;
				
				for (uint32_t candidateIndex: candidateIndices)
				{
					auto const& vertex = mwMesh->getVertex(candidateIndex);

					// Get distance to centre
					auto vertexPosition = vertex.getPosition();
					float distSq = areaCentre.distanceToSq(vertexPosition);

					if (distSq < nearestDistanceSq)
					{
						nearestDistanceSq = distSq;
						nearestVertexIndex = candidateIndex;
					}
				}

				return nearestVertexIndex;
			}
			
			//
			// Get all edges intersecting the specified bounding object.
			//
			template<typename A>
			std::set<uint32_t> getEdgeIndicesInBoundingObject(A const& area) const
			{
				std::set<uint32_t> edgeIndices;
				
				if (mwMesh->mEdgeAccelerationGrid)
				{
					// Get candidate edges in broadphase and then check those
					auto candidateEdges = mwMesh->mEdgeAccelerationGrid->getCandidateItemsInBoundingArea(area);
					for (uint32_t candidateEdge: candidateEdges)
					{
						if (areaIntersectsEdge(area, mwMesh->getEdge(candidateEdge)))
						{
							edgeIndices.insert(candidateEdge);
						}
					}
				}
				else
				{
					// Go through every edge and check
					uint32_t edgeIndex = mwMesh->getFirstEdgeIndex();
					while (!mwMesh->edgeIndexIterationFinished(edgeIndex))
					{
						if (areaIntersectsEdge(area, mwMesh->getEdge(edgeIndex)))
						{
							edgeIndices.insert(edgeIndex);
						}

						edgeIndex = mwMesh->getNextEdgeIndex(edgeIndex);
					}
				}
				
				return edgeIndices;
			}

			//
			// Get the edge indices intersecting the given line.
			//
			std::set<uint32_t> getEdgesIntersectingLine(Vector2 const& v0, Vector2 const& v1);
			
			//
			// Get the nearest edge to the specified position, within a given tolerance.
			//
			int32_t getNearestEdgeToPoint(float x, float y, float maxDistance) const
			{
				BoundingCircle circle(x, y, maxDistance);
				auto candidateIndices = getEdgeIndicesInBoundingObject(circle);

				float nearestDistance = std::numeric_limits<float>::max();
				int32_t nearestEdgeIndex = -1;

				Vector2 position(x, y);
				for (uint32_t candidateIndex: candidateIndices)
				{
					auto const& edge = mwMesh->getEdge(candidateIndex);

					// Get edge vertices
					auto const& vertex0 = mwMesh->getVertex(edge.getFirstVertex());
					auto const& vertex1 = mwMesh->getVertex(edge.getSecondVertex());

					// Get distance from (x,y) to this line
					float dist = position.distanceToLine(vertex0.getPosition(), vertex1.getPosition());
					if (dist < nearestDistance && dist <= maxDistance)
					{
						nearestDistance = dist;
						nearestEdgeIndex = candidateIndex;
					}
				}

				return nearestEdgeIndex;
			}

			//
			// Get all polygons intersecting the specified bounding object.
			//
			template<typename A>
			std::set<uint32_t> getPolygonIndicesInBoundingObject(A const& area) const
			{
				std::set<uint32_t> polygonIndices;

				if (mwMesh->mPolygonAccelerationGrid)
				{
					// Get candidate polygons in broadphase and then check those
					auto candidatePolygons = mwMesh->mPolygonAccelerationGrid->getCandidateItemsInBoundingArea(area);
					for (uint32_t candidatePolygon: candidatePolygons)
					{
						if (areaIntersectsPolygon(area, mwMesh->getPolygon(candidatePolygon)))
						{
							polygonIndices.insert(candidatePolygon);
						}
					}
				}
				else
				{
					// Go through every polygons and check
					uint32_t polygonIndex = mwMesh->getFirstPolygonIndex();
					while (!mwMesh->polygonIndexIterationFinished(polygonIndex))
					{
						if (areaIntersectsPolygon(area, mwMesh->getPolygon(polygonIndex)))
						{
							polygonIndices.insert(polygonIndex);
						}

						polygonIndex = mwMesh->getNextPolygonIndex(polygonIndex);
					}
				}

				return polygonIndices;
			}

			//
			// Get the index of the polygon containing the specified point, or -1 if none.
			//
			int32_t getPolygonContainingPoint(float x, float y);
			
		};

	} // geometry
} // WP_NAMESPACE
