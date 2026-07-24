#include <cassert>
#include <iterator>
#include <algorithm>

#include <willpower/common/StringUtils.h>

#include "willpower/common/Globals.h"
#include "willpower/common/WillpowerWalker.h"

#include "willpower/geometry/Mesh.h"
#include "willpower/geometry/MeshOperations.h"
#include "willpower/geometry/BezierOffsetter.h"
#include "willpower/geometry/ConvexOffsetter.h"
#include "willpower/geometry/clipper.hpp"

#undef max

namespace WP_NAMESPACE
{
	namespace geometry
	{
		using namespace std;
		using namespace utils;

		ExtrudeVertexOptions MeshOperations::DefaultExtrudeVertexOptions = ExtrudeVertexOptions();
		BridgeEdgesOptions MeshOperations::DefaultBridgeOptions = BridgeEdgesOptions();
		WeldEdgesOptions MeshOperations::DefaultWeldEdgeOptions = WeldEdgesOptions();
		ExtrudePolygonOptions MeshOperations::DefaultExtrudeOptions = ExtrudePolygonOptions();
		float MeshOperations::OptimalBezierCurvature = 0.551915f;

		void MeshOperations::snipCornerVertex(Mesh* mesh, uint32_t vertexIndex, uint32_t edgeIndex0, uint32_t edgeIndex1, float distance, SnipVertexResult* result)
		{
			auto vertex = mesh->getVertex(vertexIndex);

			// Check edges lengths.
			float edgeLengths[2], distances[2];
			
			uint32_t edgeIndices[2] = {
				edgeIndex0, edgeIndex1
			};

			for (int i = 0; i < 2; ++i)
			{
				auto const& edge = mesh->getEdge(edgeIndices[i]);

				edgeLengths[i] = edge.getLength();
				distances[i] = distance < 0.0f ? edgeLengths[i] : min(distance, edgeLengths[i]);
			}

			// Order edges
			if (mesh->getEdge(edgeIndices[0]).getFirstVertex() == mesh->getEdge(edgeIndices[1]).getSecondVertex())
			{
				swap(edgeLengths[0], edgeLengths[1]);
				swap(edgeIndices[0], edgeIndices[1]);
				swap(distances[0], distances[1]);
			}

			if (result)
			{
				result->vertexIndex = vertexIndex;

				auto const& edge = mesh->getEdge(edgeIndices[0]);
				result->affectedPolygon = *edge.getPolygonReferences().begin();
			}

			// If both edge lengths are exactly distance, then we need to remove the vertex.
			// If one is distance, we need to move the vertex.  If neither are, we insert
			// one new vertex.
			if (edgeLengths[0] == distances[0] && edgeLengths[1] == distances[1])
			{
				RemoveVertexResult removeResult;
				mesh->removeVertex(vertexIndex, &removeResult);

				if (result)
				{
					result->newEdgeIndex = removeResult.newEdgeIndex;
				}
			}
			else
			{
				if (edgeLengths[0] == distances[0])
				{
					if (result)
					{
						result->newEdgeIndex = edgeIndices[0];
					}

					// Move along edge1
					auto const& edge = mesh->getEdge(edgeIndices[1]);
					mesh->moveVertexTo(vertexIndex, edge.lerp(distance / edgeLengths[1]));
				}
				else if (edgeLengths[1] == distances[1])
				{
					if (result)
					{
						result->newEdgeIndex = edgeIndices[1];
					}

					// Move along edge0
					auto const& edge = mesh->getEdge(edgeIndices[0]);
					mesh->moveVertexTo(vertexIndex, edge.lerp(1.0f - (distance / edgeLengths[0])));
				}
				else
				{
					// Move one vertex, create another
					auto const& edge0 = mesh->getEdge(edgeIndices[0]);
					auto const& edge1 = mesh->getEdge(edgeIndices[1]);

					Vector2 p0 = edge0.lerp(1.0f - (distance / edgeLengths[0]));
					Vector2 p1 = edge1.lerp(distance / edgeLengths[1]);

					mesh->moveVertexTo(vertexIndex, p0);

					SplitEdgeResult splitResult;
					MeshOperations::splitEdge(mesh, edgeIndices[1], 0.5f, &splitResult);

					mesh->moveVertexTo(splitResult.newVertexIndices.front(), p1);

					if (result)
					{
						result->newEdgeIndex = splitResult.newEdgeIndices[0];
					}
				}
			}
		}

		void MeshOperations::extrudeVertexExternal(Mesh* mesh, uint32_t vertexIndex, float distance, ExtrudeVertexOptions options, ExtrudeVertexResult* result)
		{
			auto const& vertex = mesh->getVertex(vertexIndex);

			Vector2 cornerPos = vertex.getPosition();

			auto const& edgeRefs = vertex.getEdgeReferences();
			auto edgeIt = edgeRefs.begin();

			auto edgeIndex0 = *edgeIt; edgeIt++;
			auto edgeIndex1 = *edgeIt;

			SnipVertexResult svr;
			snipCornerVertex(mesh, vertexIndex, edgeIndex0, edgeIndex1, distance, &svr);

			// TODO: when outwards==true, does inwards correctly.  When outwards==false, does outwards incorrectly.
			// TODO: check order of edge vertices is always consistent (should be)

			// These have been flipped.
			uint32_t v0Index = mesh->getEdge(svr.newEdgeIndex).getFirstVertex();
			uint32_t v1Index = mesh->getEdge(svr.newEdgeIndex).getSecondVertex();

			Vector2 startPos = mesh->getVertex(v0Index).getPosition();
			Vector2 endPos = mesh->getVertex(v1Index).getPosition();

			vector<Vector2> newVertexPositions;
			if (options.type == ExtrudeVertexOptions::Type::Round)
			{
				// Create an arc from startPos to endPos with cornerPos as the centre
				Vector2 arcDir0 = startPos - cornerPos;
				Vector2 arcDir1 = endPos - cornerPos;

				float scale = arcDir0.length();

				if (options.outwards)
				{
					float angle = arcDir0.anticlockwiseAngleTo(arcDir1);
					int numPoints = (int)(scale * 1.5f * angle / 360.0f);
					for (int i = 0; i < numPoints; ++i)
					{
						Vector2 rotated = arcDir0.rotatedAnticlockwiseCopy(angle * i / (numPoints - 1));
						newVertexPositions.push_back(cornerPos + rotated);
					}
				}
				else
				{
					float angle = arcDir0.clockwiseAngleTo(arcDir1);
					int numPoints = (int)(scale * 1.5f * angle / 360.0f);
					for (int i = 0; i < numPoints; ++i)
					{
						Vector2 rotated = arcDir0.rotatedClockwiseCopy(angle * i / (numPoints - 1));
						newVertexPositions.push_back(cornerPos + rotated);
					}
				}
			}
			else if (options.type == ExtrudeVertexOptions::Type::Square)
			{
				float threshold = distance * options.squareThreshold;

				// Extrude v0Index and v1Index along their edge normal
				uint32_t polyIndex = *mesh->getEdge(svr.newEdgeIndex).getPolygonReferences().begin();
				pair<uint32_t, uint32_t> neighbourEdgeIndices = mesh->getPolygon(polyIndex).getNeighbourEdges(svr.newEdgeIndex);

				// Polygon is external so goes in opposite direction to edges here, so have to swap order.
				auto const& nEdge0 = mesh->getEdge(neighbourEdgeIndices.second);
				auto const& nEdge1 = mesh->getEdge(neighbourEdgeIndices.first);

				Vector2 normal0 = options.outwards ? nEdge0.getNormal() : -nEdge0.getNormal();
				Vector2 normal1 = options.outwards ? nEdge1.getNormal() : -nEdge1.getNormal();

				Vector2 v0o = startPos + normal0 * distance;
				Vector2 v1o = endPos + normal1 * distance;

				newVertexPositions.push_back(startPos);
				newVertexPositions.push_back(v0o);

				// if these extrusions intersect, then we are done
				LineHit hit;
				if (MathsUtils::lineLineIntersection(startPos, v0o, endPos, v1o, &hit) == MathsUtils::LineIntersectionType::Intersecting)
				{
					newVertexPositions.push_back(hit.getPosition());
				}
				else
				{
					if (options.outwards)
					{
						// Project along their perpendicular
						Vector2 v0d = nEdge0.getDirection();
						Vector2 v1d = -nEdge1.getDirection();

						LineHit hit2;
						if (MathsUtils::rayRayIntersection(v0o, v0d, v1o, v1d, &hit2) == MathsUtils::LineIntersectionType::Intersecting)
						{
							// Get distance
							float rayDist = hit2.getPosition().distanceTo(v0o);
							if (rayDist > threshold)
							{
								newVertexPositions.push_back(v0o + v0d * threshold);
								newVertexPositions.push_back(v1o + v1d * threshold);
							}
							else
							{
								newVertexPositions.push_back(hit2.getPosition());
							}
						}
						else
						{
							newVertexPositions.push_back(v0o + v0d * threshold);
							newVertexPositions.push_back(v1o + v1d * threshold);
						}
					}
				}

				newVertexPositions.push_back(v1o);
				newVertexPositions.push_back(endPos);
			}
			else
			{
				throw GeometryOperationException(__FUNCTION__, "unknown extrusion type.", false);
			}

			// Split into sub edges
			SplitEdgeResult splitResult;
			MeshOperations::splitEdge(mesh, svr.newEdgeIndex, (int)newVertexPositions.size() - 1, &splitResult);

			for (int i = 0; i < (int)newVertexPositions.size() - 2; ++i)
			{
				mesh->moveVertexTo(splitResult.newVertexIndices[i], newVertexPositions[i + 1]);
			}

			if (result)
			{
				result->vertexIndex = vertexIndex;

				auto edgeRefIndex = *mesh->getVertex(vertexIndex).getEdgeReferences().begin();
				auto const& polyRefs = mesh->getEdge(edgeRefIndex).getPolygonReferences();

				result->affectedPolygon = *polyRefs.begin();
				result->newVertexIndices = splitResult.newVertexIndices;
				result->newVertexIndices.push_back(v0Index);
				result->newVertexIndices.push_back(v1Index);
				result->newEdgeIndices = splitResult.newEdgeIndices;
			}
		}

		void MeshOperations::extrudeVertex(Mesh* mesh, uint32_t vertexIndex, float distance, ExtrudeVertexOptions options, ExtrudeVertexResult* result)
		{
			auto& vertex = mesh->_getVertex(vertexIndex);

			// Check only two edges, and that they are external
			auto const& edgeRefs = vertex.getEdgeReferences();
			if (edgeRefs.size() != 2)
			{
				string errMsg = std::format("vertex {} is not used by two edges.", vertexIndex);
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			}

			for (uint32_t edgeIndex : edgeRefs)
			{
				if (mesh->getEdge(edgeIndex).getConnectivity() != Edge::Connectivity::External)
				{
					string errMsg = std::format("edge {} is not external.", edgeIndex);
					throw GeometryOperationException(__FUNCTION__, errMsg, true);
				}
			}

			extrudeVertexExternal(mesh, vertexIndex, distance, options, result);
		}

		void MeshOperations::chamferVertex(Mesh* mesh, uint32_t vertexIndex, float distance, float tightness, ChamferVertexResult* result)
		{
			auto const& vertex = mesh->getVertex(vertexIndex);
			Vector2 cornerPos = vertex.getPosition();

			SnipVertexResult svr;
			snipVertex(mesh, vertexIndex, distance, &svr);

			uint32_t v0Index = mesh->getEdge(svr.newEdgeIndex).getFirstVertex();
			uint32_t v1Index = mesh->getEdge(svr.newEdgeIndex).getSecondVertex();

			// Get vectors to the corner and set control point positions.
			Vector2 startPos = mesh->getVertex(v0Index).getPosition();
			Vector2 endPos = mesh->getVertex(v1Index).getPosition();
			Vector2 ctrlPos0 = startPos.lerp(cornerPos, tightness);
			Vector2 ctrlPos1 = endPos.lerp(cornerPos, tightness);

			// Create bezier
			BezierSpline curve({ startPos, ctrlPos0, ctrlPos1, endPos });
			auto curvePoints = curve.divide(true, 1.0f);

			// Split into sub edges
			SplitEdgeResult splitResult;
			MeshOperations::splitEdge(mesh, svr.newEdgeIndex, (int)curvePoints.size() - 1, &splitResult);

			for (int i = 0; i < (int)curvePoints.size() - 2; ++i)
			{
				mesh->moveVertexTo(splitResult.newVertexIndices[i], curvePoints[i + 1]);
			}

			if (result)
			{
				result->vertexIndex = vertexIndex;

				auto edgeId = *mesh->getVertex(vertexIndex).getEdgeReferences().begin();
				result->affectedPolygon = *mesh->getEdge(edgeId).getPolygonReferences().begin();

				result->newVertexIndices.clear();
				result->newVertexIndices.push_back(v0Index);
				copy(splitResult.newVertexIndices.begin(), splitResult.newVertexIndices.end(),
					back_inserter(result->newVertexIndices));
				result->newVertexIndices.push_back(v1Index);

				result->newEdgeIndices = splitResult.newEdgeIndices;
			}
		}

		void MeshOperations::snipVertex(Mesh* mesh, uint32_t vertexIndex, float distance, SnipVertexResult* result)
		{
			auto const& vertex = mesh->getVertex(vertexIndex);

			vector<uint32_t> externalEdgeIndices;
			for (uint32_t edgeIndex : vertex.getEdgeReferences())
			{
				if (mesh->getEdge(edgeIndex).getConnectivity() == Edge::Connectivity::External)
				{
					externalEdgeIndices.push_back(edgeIndex);
				}
			}

			// Check that vertex has only two external edge refs
			ASSERT_TRACE(externalEdgeIndices.size() == 2 && "Mesh::snipVertex(): vertex needs two external edge references.");

			// Check that vertex is not collinear
			if (mesh->isVertexCollinearOrphan(vertexIndex, 0.0f))
			{
				return;
			}

			// Snip vertex
			snipCornerVertex(mesh, vertexIndex, externalEdgeIndices[0], externalEdgeIndices[1], distance, result);
		}

		void MeshOperations::splitEdge(Mesh* mesh, uint32_t edgeIndex, float t, SplitEdgeResult* result)
		{
			ASSERT_TRACE(t >= 0 && t <= 1 && "Mesh::splitEdge() t is out of bounds.");

			auto& splitEdge = mesh->_getEdge(edgeIndex);

			int32_t vertexIndex0 = splitEdge.getFirstVertex();
			int32_t vertexIndex1 = splitEdge.getSecondVertex();

			Vector2 splittingVertexPos = splitEdge.lerp(t);
			uint32_t newVertexIndex = mesh->addVertex(Vertex(splittingVertexPos), vertexIndex0);

			// Set old edge to end at new vertex
			splitEdge.setSecondVertex(newVertexIndex);

			// Create a new edge to fill the gap on the other side.
			uint32_t newEdgeIndex = mesh->addEdge(Edge(newVertexIndex, vertexIndex1), edgeIndex);

			// Fix up the polygons.  Re-get the splitEdge to avoid relocation issues
			// due to vector resizing.
			auto const& resplitEdge = mesh->getEdge(edgeIndex);
			auto const& polygonRefs = resplitEdge.getPolygonReferences();
			for (uint32_t polygonRef : polygonRefs)
			{
				auto& polygon = mesh->_getPolygon(polygonRef);
				auto edgeIt = polygon.getEdgeByIndex(edgeIndex);

				uint32_t oldIndex0 = (*edgeIt).v0;
				uint32_t oldIndex1 = (*edgeIt).v1;

				// Swap endpoints depending on whether this polygon uses the edge in the right direction.
				if ((uint32_t)resplitEdge.getFirstVertex() == (*edgeIt).v0)
				{
					polygon.updateEdgeSecondVertex(edgeIt, newVertexIndex);
					polygon.addEdge(newVertexIndex, oldIndex1, newEdgeIndex);
				}
				else
				{
					polygon.updateEdgeFirstVertex(edgeIt, newVertexIndex);
					polygon.addEdge(oldIndex0, newVertexIndex, newEdgeIndex);
				}
			}

#ifdef CHECK_INTEGRITY
			mesh->checkIntegrity();
#endif

			if (result)
			{
				result->splitEdgeIndex = edgeIndex;

				result->splitEdgeVertexIndices[0] = vertexIndex0;
				result->splitEdgeVertexIndices[1] = vertexIndex1;

				result->affectedPolygons.clear();
				copy(polygonRefs.begin(), polygonRefs.end(), back_inserter(result->affectedPolygons));

				result->newVertexIndices.clear();
				result->newVertexIndices.push_back(newVertexIndex);

				result->newEdgeIndices.clear();
				result->newEdgeIndices.push_back(edgeIndex);
				result->newEdgeIndices.push_back(newEdgeIndex);
			}
		}

		void MeshOperations::splitEdge(Mesh* mesh, uint32_t edgeIndex, int edgeCount, SplitEdgeResult* result)
		{
			if (edgeCount < 2)
			{
				return;
			}

			auto& splitEdge = mesh->_getEdge(edgeIndex);
			uint32_t v0 = splitEdge.getFirstVertex();
			uint32_t v1 = splitEdge.getSecondVertex();

			// Create new vertices between the edge vertices
			vector<uint32_t> newVertexIndices;
			for (int i = 0; i < edgeCount - 1; ++i)
			{
				float t = (i + 1) / (float)edgeCount;
				Vector2 vertexPos = splitEdge.lerp(t);

				uint32_t newVertexIndex = mesh->addVertex(Vertex(vertexPos));

				newVertexIndices.push_back(newVertexIndex);
			}

			// Create new edges
			splitEdge.setSecondVertex(newVertexIndices[0]);

			vector<uint32_t> newEdgeIndices;
			for (int i = 0; i < edgeCount - 1; ++i)
			{
				uint32_t newEdgeIndex;
				if (i < (edgeCount - 2))
				{
					newEdgeIndex = mesh->addEdge(Edge(newVertexIndices[i], newVertexIndices[i + 1]));
				}
				else
				{
					newEdgeIndex = mesh->addEdge(Edge(newVertexIndices[i], v1));
				}

				newEdgeIndices.push_back(newEdgeIndex);
			}

			// Fix up the polygons.  Re-get the splitEdge to avoid relocation issues
			// due to vector resizing.
			auto const& resplitEdge = mesh->getEdge(edgeIndex);
			auto const& polygonRefs = resplitEdge.getPolygonReferences();
			for (uint32_t polygonRef : polygonRefs)
			{
				auto& polygon = mesh->_getPolygon(polygonRef);
				auto edgeIt = polygon.getEdgeByIndex(edgeIndex);

				// Swap endpoints depending on whether this polygon uses the edge in the right direction.
				if ((uint32_t)resplitEdge.getFirstVertex() == (*edgeIt).v0)
				{
					polygon.updateEdgeSecondVertex(edgeIt, newVertexIndices[0]);

					for (uint32_t eIndex : newEdgeIndices)
					{
						auto const& edge = mesh->getEdge(eIndex);
						polygon.addEdge(edge.getFirstVertex(), edge.getSecondVertex(), eIndex);
					}
				}
				else
				{
					polygon.updateEdgeFirstVertex(edgeIt, newVertexIndices[0]);

					for (uint32_t eIndex : newEdgeIndices)
					{
						auto const& edge = mesh->getEdge(eIndex);
						polygon.addEdge(edge.getSecondVertex(), edge.getFirstVertex(), eIndex);
					}
				}
			}

#ifdef CHECK_INTEGRITY
			mesh->checkIntegrity();
#endif

			if (result)
			{
				result->splitEdgeIndex = edgeIndex;

				result->splitEdgeVertexIndices[0] = v0;
				result->splitEdgeVertexIndices[1] = v1;

				result->affectedPolygons.clear();
				copy(polygonRefs.begin(), polygonRefs.end(), back_inserter(result->affectedPolygons));
				
				result->newVertexIndices = newVertexIndices;

				result->newEdgeIndices.clear();
				result->newEdgeIndices.push_back(edgeIndex);
				copy(newEdgeIndices.begin(), newEdgeIndices.end(), back_inserter(result->newEdgeIndices));
			}
		}

		void MeshOperations::setEdgeLength(Mesh* mesh, uint32_t edgeIndex, float length, SetEdgeLengthResult* result)
		{
			auto const& edge = mesh->getEdge(edgeIndex);

			auto v0i = edge.getFirstVertex();
			auto v1i = edge.getSecondVertex();

			auto& v0 = mesh->_getVertex(v0i);
			auto& v1 = mesh->_getVertex(v1i);

			auto centre = edge.getCentre();
			auto const extent2 = edge.getDirection() * length / 2;

			v0.setPosition(centre - extent2);
			v1.setPosition(centre + extent2);

			if (result)
			{
				result->edgeIndex = edgeIndex;

				result->splitEdgeVertexIndices[0] = v0i;
				result->splitEdgeVertexIndices[1] = v1i;

				auto const& polygonRefs = edge.getPolygonReferences();
				result->affectedPolygons.clear();
				copy(polygonRefs.begin(), polygonRefs.end(), back_inserter(result->affectedPolygons));
			}
		}

		void MeshOperations::bridgeEdges(Mesh* mesh, uint32_t sourceEdgeIndex, uint32_t targetEdgeIndex, BridgeEdgesOptions options, BridgeEdgesResult* result)
		{
			vector<uint32_t> sourceEdgeIndices = { sourceEdgeIndex };
			vector<uint32_t> targetEdgeIndices = { targetEdgeIndex };
			return bridgeEdges(mesh, sourceEdgeIndices, targetEdgeIndices, options, result);
		}

		void MeshOperations::bridgeEdges(Mesh* mesh, IndexVector const& sourceEdgeIndices, IndexVector const& targetEdgeIndices, BridgeEdgesOptions options, BridgeEdgesResult* result)
		{
			// We can have edge lists from different polygons, but if this happens, we can't 
			// merge the result.
			auto sourcePolygonRefs = MeshUtils::getPolygonReferences(mesh, sourceEdgeIndices);
			auto targetPolygonRefs = MeshUtils::getPolygonReferences(mesh, targetEdgeIndices);

			if (sourcePolygonRefs.size() > 1 || targetPolygonRefs.size() > 1 && options.merge)
			{
				string errMsg = "cannot merge a bridge operation when an edge list spans more than one polygon.";
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			}

			// Check all edges are external
			for (uint32_t edgeIndex : sourceEdgeIndices)
			{
				auto const& edge = mesh->getEdge(edgeIndex);
				if (edge.getConnectivity() != Edge::Connectivity::External)
				{
					string errMsg = std::format("cannot bridge edge {} as it is not an external edge.", edgeIndex);
					throw GeometryOperationException(__FUNCTION__, errMsg, true);
				}
			}

			for (uint32_t edgeIndex : targetEdgeIndices)
			{
				auto const& edge = mesh->getEdge(edgeIndex);
				if (edge.getConnectivity() != Edge::Connectivity::External)
				{
					string errMsg = std::format("cannot bridge edge {} as it is not an external edge.", edgeIndex);
					throw GeometryOperationException(__FUNCTION__, errMsg, true);
				}
			}

			uint32_t polygon0 = *sourcePolygonRefs.begin();
			uint32_t polygon1 = *targetPolygonRefs.begin();

			// Sort out squeeze - having it at -1 can break triangulation due to collinear edges.
			if (options.squeezeAmount <= -1.0f)
			{
				options.squeezeAmount = -0.99f;
			}

			// Join the edges up
			MeshUtils::EdgeInfoList sourceEdgeInfo = MeshUtils::getEdgeInfo(sourceEdgeIndices, mesh);
			MeshUtils::OrderedEdgeGroupSet orderedSourceEdges = MeshUtils::groupConnectedEdges(sourceEdgeInfo);

			MeshUtils::EdgeInfoList targetEdgeInfo = MeshUtils::getEdgeInfo(targetEdgeIndices, mesh);
			MeshUtils::OrderedEdgeGroupSet orderedTargetEdges = MeshUtils::groupConnectedEdges(targetEdgeInfo);

			ASSERT_TRACE(orderedSourceEdges.size() == 1 && orderedTargetEdges.size() == 1 && "Mesh::bridgeEdges(): edge list(s) are not contiguous.");

			auto const& sourceEdges = orderedSourceEdges[0];
			auto const& targetEdges = orderedTargetEdges[0];

			// Check that the edges do not join up (ie no loops) and that they don't bend over
			// 180 degrees (which would cause self-intersection when bridging).
			ASSERT_TRACE(get<1>(sourceEdges.front()) != get<2>(sourceEdges.back()) && "Mesh::bridgeEdges(): source edge list cannot be an unbroken loop.");
			ASSERT_TRACE(get<1>(targetEdges.front()) != get<2>(targetEdges.back()) && "Mesh::bridgeEdges(): target edge list cannot be an unbroken loop.");

			Vector2 sourceStartNormal = mesh->getEdge(get<0>(sourceEdges.front())).getNormal();
			Vector2 sourceEndNormal = mesh->getEdge(get<0>(sourceEdges.back())).getNormal();

			float sourceAngle = sourceStartNormal.anticlockwiseAngleTo(sourceEndNormal);
			if (sourceAngle >= 180.0f)
			{
				string errMsg = "source edges bend over 180 degrees.";
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			}

			Vector2 targetStartNormal = mesh->getEdge(get<0>(targetEdges.front())).getNormal();
			Vector2 targetEndNormal = mesh->getEdge(get<0>(targetEdges.back())).getNormal();

			float targetAngle = targetStartNormal.anticlockwiseAngleTo(targetEndNormal);
			if (targetAngle >= 180.0f)
			{
				string errMsg = "target edges bend over 180 degrees.";
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			}

			// Add (steps - 1) vertices, interpolating from source end to target start for list1,
			// and from target end to source start for list2.  Then create edges, and the new polygon
			// will use the edge list with reversed direction.
			uint32_t sourceStartVertex = get<1>(sourceEdges[0]);
			uint32_t sourceEndVertex = get<2>(*--sourceEdges.end());
			uint32_t targetStartVertex = get<1>(targetEdges[0]);
			uint32_t targetEndVertex = get<2>(*--targetEdges.end());

			Vector2 start1 = mesh->getVertex(targetStartVertex).getPosition();
			Vector2 end1 = mesh->getVertex(sourceEndVertex).getPosition();
			Vector2 start2 = mesh->getVertex(sourceStartVertex).getPosition();
			Vector2 end2 = mesh->getVertex(targetEndVertex).getPosition();

			IndexVector edgeList1, edgeList2;
			uint32_t prevV1Index = targetStartVertex;
			uint32_t prevV2Index = sourceStartVertex;

			// Set squeeze function
			auto squeezeFn = [options](float t) { return options.squeezeAmount + (cosf(WP_DEGTORAD(t * 360.0f)) + 1) * 0.5f * (1 - options.squeezeAmount); };

			if (options.type == BridgeEdgesOptions::Type::Straight)
			{
				for (int i = 0; i < options.steps - 1; ++i)
				{
					float t = (float)(i + 1) / options.steps;

					// If straight edge
					Vector2 vertex1pos = start1.lerp(end1, t);
					Vector2 vertex2pos = start2.lerp(end2, t);

					// Apply squeeze
					switch (options.squeezeType)
					{
					case BridgeEdgesOptions::SqueezeType::Straight:
					case BridgeEdgesOptions::SqueezeType::Curved:
					{
						Vector2 vertex1opposite = start2.lerp(end2, 1.0f - t);
						Vector2 vertex2opposite = start1.lerp(end1, 1.0f - t);

						Vector2 vertex1centre = vertex1pos.lerp(vertex1opposite, 0.5f);
						Vector2 vertex2centre = vertex2pos.lerp(vertex2opposite, 0.5f);

						float squeeze = 1.0f;
						if (options.squeezeType == BridgeEdgesOptions::SqueezeType::Curved)
						{
							squeeze *= squeezeFn(t);
						}

						vertex1pos = vertex1pos.lerp(vertex1centre, squeeze);
						vertex2pos = vertex2pos.lerp(vertex2centre, squeeze);

						break;
					}
					}

					// Create vertices
					uint32_t v1Index = mesh->addVertex(Vertex(vertex1pos));
					uint32_t v2Index = mesh->addVertex(Vertex(vertex2pos));

					// Create edges
					edgeList1.push_back(mesh->addEdge(Edge(prevV1Index, v1Index)));
					edgeList2.push_back(mesh->addEdge(Edge(prevV2Index, v2Index)));

					prevV1Index = v1Index;
					prevV2Index = v2Index;
				}
			}
			else if (options.type == BridgeEdgesOptions::Type::Curved)
			{
				// Endpoints are the centres of the edges
				Vector2 endpoints[2];
				uint32_t endpointEdgeIndices[2];

				endpoints[0] = MeshUtils::calculateEdgeListCentre(mesh, sourceEdges, &endpointEdgeIndices[0]);
				endpoints[1] = MeshUtils::calculateEdgeListCentre(mesh, targetEdges, &endpointEdgeIndices[1]);

				// Work out where to place control points.  Project normal a certain distance, 
				// and use these points as controls.
				Vector2 ctrlPoints[2];

				Vector2 p0d = -mesh->getEdge(endpointEdgeIndices[0]).getNormal();
				Vector2 p1d = -mesh->getEdge(endpointEdgeIndices[1]).getNormal();

				float d, endDist = endpoints[0].distanceTo(endpoints[1]);

				LineHit hit;
				if (MathsUtils::rayRayIntersection(endpoints[0], p0d, endpoints[1], p1d, &hit) == MathsUtils::LineIntersectionType::Intersecting)
				{
					float d0 = hit.getPosition().distanceTo(endpoints[0]);
					float d1 = hit.getPosition().distanceTo(endpoints[1]);
					d = min(d0, d1);

					if (d > endDist)
					{
						d = endDist;
					}
				}
				else
				{
					d = endDist;
				}

				ctrlPoints[0] = endpoints[0] + p0d * d * options.tightness;
				ctrlPoints[1] = endpoints[1] + p1d * d * options.tightness;

				// Get widths
				float ws = mesh->getVertex(get<1>(sourceEdges.front())).getPosition().distanceTo(mesh->getVertex(get<2>(sourceEdges.back())).getPosition());
				float wt = mesh->getVertex(get<1>(targetEdges.front())).getPosition().distanceTo(mesh->getVertex(get<2>(targetEdges.back())).getPosition());

				// Create bezier offsetter
				BezierOffsetter offsetter({ endpoints[0], ctrlPoints[0], ctrlPoints[1], endpoints[1] }, 1.0f, true);

				switch (options.squeezeType)
				{
				case BridgeEdgesOptions::SqueezeType::None:
				case BridgeEdgesOptions::SqueezeType::Straight:
					offsetter.offset(ws / 2, wt / 2, Offsetter::CornerType::Square);
					break;
				case BridgeEdgesOptions::SqueezeType::Curved:
					offsetter.offset(ws / 2, wt / 2, Offsetter::CornerType::Square, squeezeFn);
					break;
				default:
					offsetter.offset(ws / 2, wt / 2, Offsetter::CornerType::Square);
					break;
				}

				auto vertices = offsetter.getOffsetVertices();
				auto& side0 = vertices[0];
				auto& side1 = vertices[1];

				// Remove front and back
				rotate(side0.begin(), ++side0.begin(), side0.end()); side0.pop_back(); side0.pop_back();
				rotate(side1.begin(), ++side1.begin(), side1.end()); side1.pop_back(); side1.pop_back();

				for (auto const& pos : side1)
				{
					// Create vertex
					uint32_t v1Index = mesh->addVertex(Vertex(pos));

					// Create edge
					edgeList1.push_back(mesh->addEdge(Edge(prevV1Index, v1Index)));
					prevV1Index = v1Index;
				}

				for (auto const& pos : side0)
				{
					// Create vertex
					uint32_t v2Index = mesh->addVertex(Vertex(pos));

					// Create edge
					edgeList2.push_back(mesh->addEdge(Edge(prevV2Index, v2Index)));
					prevV2Index = v2Index;
				}
			}

			// Create final edges
			edgeList1.push_back(mesh->addEdge(Edge(prevV1Index, sourceEndVertex)));
			edgeList2.push_back(mesh->addEdge(Edge(prevV2Index, targetEndVertex)));

			// Create new polygon
			IndexVector edgeData;

			// Add source edges in reverse, to ensure anticlockwise
			for (auto it = sourceEdges.rbegin(); it != sourceEdges.rend(); ++it)
			{
				auto const& edgeInfo = *it;

				edgeData.push_back(get<2>(edgeInfo));
				edgeData.push_back(get<1>(edgeInfo));
				edgeData.push_back(get<0>(edgeInfo));
			}

			// Add second edge list
			for (auto it = edgeList2.begin(); it != edgeList2.end(); ++it)
			{
				uint32_t edgeIndex = *it;
				auto const& edge = mesh->getEdge(edgeIndex);

				edgeData.push_back(edge.getFirstVertex());
				edgeData.push_back(edge.getSecondVertex());
				edgeData.push_back(edgeIndex);
			}

			// Add target edges in reverse, to ensure anticlockwise
			for (auto it = targetEdges.rbegin(); it != targetEdges.rend(); ++it)
			{
				auto const& edgeInfo = *it;

				edgeData.push_back(get<2>(edgeInfo));
				edgeData.push_back(get<1>(edgeInfo));
				edgeData.push_back(get<0>(edgeInfo));
			}

			// Add first edge list
			for (auto it = edgeList1.begin(); it != edgeList1.end(); ++it)
			{
				uint32_t edgeIndex = *it;
				auto const& edge = mesh->getEdge(edgeIndex);

				edgeData.push_back(edge.getFirstVertex());
				edgeData.push_back(edge.getSecondVertex());
				edgeData.push_back(edgeIndex);
			}

			Polygon newPolygon(edgeData);

			// Add polygon
			uint32_t pIndex = mesh->addPolygon(newPolygon, polygon0);

			// Merge
			if (options.merge)
			{
				mergePolygonsByEdge(mesh, polygon0, pIndex);

				if (polygon0 != polygon1)
				{
					mergePolygonsByEdge(mesh, polygon0, polygon1);
				}

				pIndex = polygon0;
			}

			if (result)
			{
				result->polygons.clear();

				BridgeEdgesResult::Polygon p;
				p.index = pIndex;

				if (!options.merge)
				{
					p.edges[0] = edgeList1;
					p.edges[1] = edgeList2;
				}

				result->polygons.push_back(p);
			
				result->polygonRemovedInMergeIndex = options.merge ? polygon1 : ~0u;
			}
		}

		void MeshOperations::weldEdges(Mesh* mesh, uint32_t edge0Index, uint32_t edge1Index, WeldEdgesOptions options, WeldEdgesResult* result)
		{
			auto const& edge0 = mesh->getEdge(edge0Index);
			auto const& edge1 = mesh->getEdge(edge1Index);

			// Edges must be external
			if (edge0.getConnectivity() != Edge::Connectivity::External ||
				edge1.getConnectivity() != Edge::Connectivity::External)
			{
				throw GeometryOperationException(__FUNCTION__, "cannot weld internal edges.", true);
			}

			// Get vertex information
			uint32_t v00Index = edge0.getFirstVertex();
			uint32_t v01Index = edge0.getSecondVertex();

			uint32_t v10Index = edge1.getFirstVertex();
			uint32_t v11Index = edge1.getSecondVertex();

			auto const& vertex00 = mesh->getVertex(v00Index);
			auto const& vertex01 = mesh->getVertex(v01Index);

			auto const& vertex10 = mesh->getVertex(v10Index);
			auto const& vertex11 = mesh->getVertex(v11Index);

			Vector2 v10pos = vertex10.getPosition();
			Vector2 v11pos = vertex11.getPosition();

			if (options.moveFirstToSecond)
			{
				mesh->moveVertexTo(v00Index, v10pos);
				mesh->moveVertexTo(v01Index, v11pos);
			}
			else
			{
				Vector2 v00pos = vertex00.getPosition();
				Vector2 v01pos = vertex01.getPosition();

				Vector2 new0pos = v00pos.lerp(v10pos, 0.5f);
				Vector2 new1pos = v01pos.lerp(v11pos, 0.5f);

				mesh->moveVertexTo(v00Index, new0pos);
				mesh->moveVertexTo(v01Index, new1pos);
				mesh->moveVertexTo(v10Index, new0pos);
				mesh->moveVertexTo(v11Index, new1pos);
			}

			// Having moved the edges in place, if merging polygons, remove both edges,
			// and stitch the two open polygons together.  If not merging, then just
			// remove one edge and fix up the broken polygon with the other edge.
			if (options.mergePolygons)
			{
				// ...
			}
			else
			{
				uint32_t polygon0Index = *edge0.getPolygonReferences().begin();
				uint32_t polygon1Index = *edge1.getPolygonReferences().begin();

				// Alter polygon edge
				auto& polygon0 = mesh->_getPolygon(polygon0Index);
				polygon0.replaceEdge(edge0Index, edge1Index, v11Index, v10Index, true, false);

				// Alter edges

				if (result)
				{
					result->weldingEdges[0] = edge0Index;
					result->weldingEdges[1] = edge1Index;
					result->polygonIndices[0] = polygon0Index;
					result->polygonIndices[1] = polygon1Index;
					result->weldedEdge = edge1Index;
				}
			}
		}

		void MeshOperations::splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t vertex2Index, SplitPolygonResult* result)
		{
			splitPolygon(mesh, polyIndex, vertex1Index, vertex2Index, IndexVector(), result);
		}

		void MeshOperations::splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t vertex2Index, SplitPolygonResult* result)
		{
			SplitEdgeResult splitResult;
			MeshOperations::splitEdge(mesh, edge1Index, edge1Amt, &splitResult);

			splitPolygon(mesh, polyIndex, splitResult.newVertexIndices.front(), vertex2Index, result);
		}

		void MeshOperations::splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t vertex2Index, IndexVector const& vertexIndices, SplitPolygonResult* result)
		{
			SplitEdgeResult splitResult;
			MeshOperations::splitEdge(mesh, edge1Index, edge1Amt, &splitResult);

			splitPolygon(mesh, polyIndex, splitResult.newVertexIndices.front(), vertex2Index, vertexIndices, result);
		}

		void MeshOperations::splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t edge2Index, float edge2Amt, SplitPolygonResult* result)
		{
			SplitEdgeResult splitResult;
			MeshOperations::splitEdge(mesh, edge2Index, edge2Amt, &splitResult);

			splitPolygon(mesh, polyIndex, vertex1Index, splitResult.newVertexIndices.front(), result);
		}

		void MeshOperations::splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t edge2Index, float edge2Amt, IndexVector const& vertexIndices, SplitPolygonResult* result)
		{
			SplitEdgeResult splitResult;
			MeshOperations::splitEdge(mesh, edge2Index, edge2Amt, &splitResult);

			splitPolygon(mesh, polyIndex, vertex1Index, splitResult.newVertexIndices.front(), vertexIndices, result);
		}

		void MeshOperations::splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t edge2Index, float edge2Amt, SplitPolygonResult* result)
		{
			SplitEdgeResult split1Result, split2Result;

			MeshOperations::splitEdge(mesh, edge1Index, edge1Amt, &split1Result);
			MeshOperations::splitEdge(mesh, edge2Index, edge2Amt, &split2Result);

			splitPolygon(mesh, polyIndex, split1Result.newVertexIndices.front(), split2Result.newVertexIndices.front(), result);
		}

		void MeshOperations::splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t edge2Index, float edge2Amt, IndexVector const& vertexIndices, SplitPolygonResult* result)
		{
			SplitEdgeResult split1Result, split2Result;

			MeshOperations::splitEdge(mesh, edge1Index, edge1Amt, &split1Result);
			MeshOperations::splitEdge(mesh, edge2Index, edge2Amt, &split2Result);

			splitPolygon(mesh, polyIndex, split1Result.newVertexIndices.front(), split2Result.newVertexIndices.front(), vertexIndices, result);
		}

		void MeshOperations::splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t vertex2Index, IndexVector const& vertexIndices, SplitPolygonResult* result)
		{
			// - Splitting a polygon must not leave any double-sided edges.
			// - It should not intersect any holes, or other polygon edges.
			// - Vertices should not share an edge.

			// Check shared edges
			auto const& v1e = mesh->getVertex(vertex1Index).getEdgeReferences();
			auto const& v2e = mesh->getVertex(vertex2Index).getEdgeReferences();

			IndexVector sharedEdgeIndices;
			set_intersection(v1e.begin(), v1e.end(), v2e.begin(), v2e.end(), back_inserter(sharedEdgeIndices));

			if (!sharedEdgeIndices.empty())
			{
				string errMsg = std::format("vertex {} and vertex {} share an edge.", vertex1Index, vertex2Index);
				throw GeometryOperationInvalidArgument(__FUNCTION__, "vertex1Index|vertex2Index", errMsg);
			}

			// Cut the polygon
			auto& polygon = mesh->_getPolygon(polyIndex);

			// Store holes for now, so we can work out which have been reassigned
			IndexList oldHoleIndices = polygon.getHoleIndices();

			IndexVector newEdgeIndices;
			DirectedEdgeVector removedEdges;
			polygon.cut(vertex1Index, vertex2Index, vertexIndices, &newEdgeIndices, &removedEdges);

			// Create a new polygon from remaining edges
			IndexVector edgeData;
			for (auto const& edge : removedEdges)
			{
				edgeData.push_back(edge.v0);
				edgeData.push_back(edge.v1);
				edgeData.push_back(edge.index);
			}

			for (auto edgeIt = newEdgeIndices.rbegin(); edgeIt != newEdgeIndices.rend(); ++edgeIt)
			{
				auto const& cutEdge = mesh->getEdge(*edgeIt);

				edgeData.push_back(cutEdge.getSecondVertex());
				edgeData.push_back(cutEdge.getFirstVertex());
				edgeData.push_back(*edgeIt);
			}

			Polygon newPolygon(edgeData);
			uint32_t newIndex = mesh->addPolygon(newPolygon);

			// Reassign holes: anything in 'old' which not in 'new'.
			IndexList removedHoleIndices;
			auto const& newHoleIndices = mesh->getPolygon(polyIndex).getHoleIndices();
			set_difference(oldHoleIndices.begin(), oldHoleIndices.end(), newHoleIndices.begin(), newHoleIndices.end(), back_inserter(removedHoleIndices));

			for (uint32_t holeIndex : removedHoleIndices)
			{
				mesh->addHoleToPolygon(newIndex, holeIndex);
			}

			if (result)
			{
				result->newPolygonIndex = newIndex;
				result->splittingEdgeIndices = newEdgeIndices;
			}
		}

		vector<DirectedEdgeVector> MeshOperations::getExtrusionSections(Mesh* mesh, uint32_t polygonIndex, IndexVector const& edgeIndices, bool separate, bool allowLoop)
		{
			auto const& polygon = mesh->getPolygon(polygonIndex);

			vector<DirectedEdgeVector> sections;
			if (separate)
			{
				for (uint32_t edgeIndex : edgeIndices)
				{
					DirectedEdge de = *polygon.getEdgeByIndex(edgeIndex);

					DirectedEdgeVector iv(1, de);
					sections.push_back(iv);
				}
			}
			else
			{
				auto edgeGroups = MeshUtils::groupConnectedEdges(mesh, polygonIndex, edgeIndices);
				for (auto const& edgeGroup : edgeGroups)
				{
					DirectedEdgeVector iv;
					for (auto const& edgeIndexInfo : edgeGroup)
					{
						DirectedEdge de;

						de.index = get<0>(edgeIndexInfo);
						de.v0 = get<1>(edgeIndexInfo);
						de.v1 = get<2>(edgeIndexInfo);

						iv.push_back(de);
					}

					// If it's a loop, fail
					if (!allowLoop && get<1>(edgeGroup.front()) == get<2>(edgeGroup.back()))
					{
						throw GeometryOperationException(__FUNCTION__, "cannot extrude a full loop.", true);
					}

					sections.push_back(iv);
				}
			}

			return sections;
		}

		void MeshOperations::extrudePolygonNormal(Mesh* mesh, uint32_t polygonIndex, IndexVector const& edgeIndices, float distance, ExtrudePolygonOptions options, ExtrudePolygonResult* result)
		{
			if (!MeshUtils::areEdgesInPolygon(mesh, polygonIndex, edgeIndices))
			{
				string errMsg = std::format("edge(s) are not in polygon {}", polygonIndex);
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			}

			// If not separate, break edge list into contiguous sections.
			vector<DirectedEdgeVector> sections = getExtrusionSections(mesh, polygonIndex, edgeIndices, options.separateExtrusions, true);
			doPolygonExtrusion(mesh, polygonIndex, sections, false, Vector2::UNIT_X * distance, options, result);
		}

		void MeshOperations::doPolygonExtrusion(Mesh* mesh, uint32_t polygonIndex, vector<DirectedEdgeVector> const& edgeSections, bool directed, Vector2 const& extrusion, ExtrudePolygonOptions options, ExtrudePolygonResult* result)
		{
			// Extrude each section
			bool first = true;
			for (auto const& section : edgeSections)
			{
				uint32_t collinearVertexIndices[2], endVertexIndices[2], newPolygonIndex, holeIndex;
				IndexVector sourceEdgeIndices, newEdgeIndices;

				try
				{
					if (directed)
					{
						extrudePolygonEdgesDirected(mesh, polygonIndex, section, extrusion, options, endVertexIndices, collinearVertexIndices, &newPolygonIndex, &holeIndex, &newEdgeIndices, &sourceEdgeIndices);
					}
					else
					{
						extrudePolygonEdgesNormal(mesh, polygonIndex, section, extrusion.length(), options, endVertexIndices, collinearVertexIndices, &newPolygonIndex, &holeIndex, &newEdgeIndices, &sourceEdgeIndices);
					}

					// Chamfer
					if (options.chamfer > 0.0f)
					{
						float chamferDist = extrusion.length() * options.chamfer;
						if (endVertexIndices[0] != (uint32_t)-1)
						{
							chamferVertex(mesh, endVertexIndices[0], chamferDist);
						}

						if (endVertexIndices[1] != (uint32_t)-1)
						{
							chamferVertex(mesh, endVertexIndices[1], chamferDist);
						}
					}

					// Check for collinear vertices
					if (options.mergePolygons && options.removeCollinearVertices && !options.separateExtrusions)
					{
						for (int i = 0; i < 2; ++i)
						{
							if (collinearVertexIndices[i] != (uint32_t)-1)
							{
								if (mesh->isVertexCollinearOrphan(collinearVertexIndices[i], 0.001f))
								{
									mesh->removeVertex(collinearVertexIndices[i]);
								}
							}
						}
					}

					first = false;
				}
				catch (GeometryOperationException& e)
				{
					if (!first)
					{
						e.setConsistentState(false);
					}

					throw e;
				}

				if (result)
				{
					ExtrudePolygonResult::Polygon polygon;
					polygon.index = newPolygonIndex;
					polygon.holeIndex = holeIndex;
					polygon.sourceEdgeIndices = sourceEdgeIndices;
					polygon.extrudedEdgeIndices = newEdgeIndices;

					result->polygons.push_back(polygon);
				}
			}
		}

		void MeshOperations::extrudePolygonEdgesDirected(Mesh* mesh, uint32_t polygonIndex, DirectedEdgeVector const& edgeData, Vector2 const& extrusion, ExtrudePolygonOptions options, uint32_t* endVertices, uint32_t* collinearVertices, uint32_t* newPolygonIndex, uint32_t* holeIndex, IndexVector* extrudedEdgeIndices, IndexVector* sourceEdgeIndices)
		{
			WP_UNUSED(holeIndex);

			// Get vertices
			vector<Vector2> vertexPositions;
			IndexVector vertexIndices;

			if (edgeData.front().v0 == edgeData.back().v1)
			{
				throw GeometryOperationException(__FUNCTION__, "cannot extrude a full loop.", true);
			}

			for (auto const& de : edgeData)
			{
				vertexPositions.push_back(mesh->getVertex(de.v0).getPosition());
				vertexIndices.push_back(de.v0);
			}

			auto const& edge = mesh->getEdge(edgeData.back().index);
			vertexPositions.push_back(mesh->getVertex(edge.getSecondVertex()).getPosition());
			vertexIndices.push_back(edge.getSecondVertex());

			// Extrude edges.  For directed extrusion, just make a copy of the vertices,
			// offset by the extrusion.  If we're merging, then just split the edges and
			// move the vertices.
			IndexVector newVertexIndices;
			if (options.mergePolygons)
			{
				// Split first and last edges
				SplitEdgeResult ser;
				uint32_t v0Index, v1Index;
				if (edgeData.front().index == edgeData.back().index)
				{
					MeshOperations::splitEdge(mesh, edgeData.front().index, 3, &ser);
					v0Index = ser.newVertexIndices[0];
					v1Index = ser.newVertexIndices[1];

					extrudedEdgeIndices->push_back(ser.newEdgeIndices[1]);
				}
				else
				{
					MeshOperations::splitEdge(mesh, edgeData.front().index, 2, &ser);
					v0Index = ser.newVertexIndices[0];

					extrudedEdgeIndices->push_back(ser.newEdgeIndices[1]);

					for (uint32_t i = 1; i < edgeData.size() - 1; ++i)
					{
						extrudedEdgeIndices->push_back(edgeData[i].index);
					}

					MeshOperations::splitEdge(mesh, edgeData.back().index, 2, &ser);
					v1Index = ser.newVertexIndices[0];

					extrudedEdgeIndices->push_back(ser.newEdgeIndices[0]);
				}

				// Store old end vertices for checking collinearity
				collinearVertices[0] = vertexIndices.front();
				collinearVertices[1] = vertexIndices.back();

				// Store new end vertices for tapering/chamfering
				endVertices[0] = v0Index;
				endVertices[1] = v1Index;

				// No source edges
				sourceEdgeIndices->clear();

				// Move split vertices to old positions, then replace the old
				// vertices with them
				mesh->moveVertexTo(v0Index, vertexPositions.front());
				mesh->moveVertexTo(v1Index, vertexPositions.back());

				vertexIndices.front() = v0Index;
				vertexIndices.back() = v1Index;

				// Move vertices
				for (uint32_t i = 0; i < vertexIndices.size(); ++i)
				{
					mesh->moveVertexTo(vertexIndices[i], vertexPositions[i] + extrusion);
				}

				*newPolygonIndex = polygonIndex;
			}
			else
			{
				// Create vertices
				for (Vector2 const& vertexPos : vertexPositions)
				{
					uint32_t vIndex = mesh->addVertex(Vertex(vertexPos + extrusion));
					newVertexIndices.push_back(vIndex);
				}

				// Store old end vertices for checking collinearity
				collinearVertices[0] = vertexIndices.front();
				collinearVertices[1] = vertexIndices.back();

				// Store new end vertices for tapering/chamfering
				endVertices[0] = newVertexIndices.front();
				endVertices[1] = newVertexIndices.back();

				// Source edges are those in edgeData
				for (auto const& de : edgeData)
				{
					sourceEdgeIndices->push_back(de.index);
				}

				// Create edges
				IndexVector newEdgeData;
				for (uint32_t i = 0; i < newVertexIndices.size() - 1; ++i)
				{
					uint32_t eIndex = mesh->addEdge(Edge(newVertexIndices[i], newVertexIndices[i + 1]));
					newEdgeData.push_back(newVertexIndices[i]);
					newEdgeData.push_back(newVertexIndices[i + 1]);
					newEdgeData.push_back(eIndex);

					extrudedEdgeIndices->push_back(eIndex);
				}

				// Create edges from new loop to old.
				Edge e0(edgeData.front().v0, newVertexIndices.front());
				Edge e1(Edge(newVertexIndices.back(), edgeData.back().v1));
				uint32_t e0Index = mesh->addEdge(e0);
				uint32_t e1Index = mesh->addEdge(e1);

				// Join up edges: new edges, then second connector, then old edges in
				// reverse, then first connector.
				newEdgeData.push_back(e1.getFirstVertex());
				newEdgeData.push_back(e1.getSecondVertex());
				newEdgeData.push_back(e1Index);

				for (auto it = edgeData.rbegin(); it != edgeData.rend(); ++it)
				{
					auto const& de = *it;
					newEdgeData.push_back(de.v1);
					newEdgeData.push_back(de.v0);
					newEdgeData.push_back(de.index);
				}

				newEdgeData.push_back(e0.getFirstVertex());
				newEdgeData.push_back(e0.getSecondVertex());
				newEdgeData.push_back(e0Index);

				Polygon newPolygon(newEdgeData);
				*newPolygonIndex = mesh->addPolygon(newPolygon);
			}
		}

		void MeshOperations::extrudePolygonEdgesNormal(Mesh* mesh, uint32_t polygonIndex, DirectedEdgeVector const& edgeData, float distance, ExtrudePolygonOptions options, uint32_t* endVertices, uint32_t* collinearVertices, uint32_t* newPolygonIndex, uint32_t* holeIndex, IndexVector* extrudedEdgeIndices, IndexVector* sourceEdgeIndices)
		{
			// Get vertices
			vector<Vector2> vertexPositions;

			bool isLoop = edgeData.front().v0 == edgeData.back().v1;

			for (auto const& de : edgeData)
			{
				vertexPositions.push_back(mesh->getVertex(de.v0).getPosition());
			}

			if (edgeData.front().v0 != edgeData.back().v1)
			{
				auto const& edge = mesh->getEdge(edgeData.back().index);
				vertexPositions.push_back(mesh->getVertex(edge.getSecondVertex()).getPosition());
			}

			// Extrude edges
			ConvexOffsetter offsetter(vertexPositions, 5.0f);
			if (isLoop)
			{
				offsetter.offset(distance, distance, options.cornerType, Offsetter::defaultWidthModifier, 0, 0);
			}
			else
			{
				offsetter.offset(distance, distance, options.cornerType);
			}

			auto const& extrudedVertexPositions = offsetter.getOffsetVertices()[0];

			if (options.mergePolygons)
			{
				if (isLoop)
				{
					// Extrude new polygon, and replace old polygon with it.
					// Create vertices
					IndexVector newVertexIndices;
					for (auto const& pos : extrudedVertexPositions)
					{
						newVertexIndices.push_back(mesh->addVertex(Vertex(pos)));
					}

					IndexVector newEdgeData;
					for (uint32_t i = 0; i < newVertexIndices.size() - 1; ++i)
					{
						uint32_t edgeIndex = mesh->addEdge(Edge(newVertexIndices[i], newVertexIndices[i + 1]));
						extrudedEdgeIndices->push_back(edgeIndex);

						newEdgeData.push_back(newVertexIndices[i]);
						newEdgeData.push_back(newVertexIndices[i + 1]);
						newEdgeData.push_back(edgeIndex);
					}

					// Join up loop.
					uint32_t edgeIndex = mesh->addEdge(Edge(newVertexIndices.back(), newVertexIndices.front()));
					extrudedEdgeIndices->push_back(edgeIndex);

					newEdgeData.push_back(newVertexIndices.back());
					newEdgeData.push_back(newVertexIndices.front());
					newEdgeData.push_back(edgeIndex);

					// No collinear vertices here
					collinearVertices[0] = (uint32_t)-1;
					collinearVertices[1] = (uint32_t)-1;

					// No end vertices in a loop
					endVertices[0] = (uint32_t)-1;
					endVertices[1] = (uint32_t)-1;

					// No source edges
					sourceEdgeIndices->clear();

					// Create polygon
					Polygon newPolygon(newEdgeData);
					*newPolygonIndex = mesh->addPolygon(newPolygon);

					// Remove old polygon, transferring hole ownership
					auto holeIndices = mesh->getPolygon(polygonIndex).getHoleIndices();
					mesh->removePolygon(polygonIndex, false);

					for (uint32_t hIndex : holeIndices)
					{
						mesh->addHoleToPolygon(*newPolygonIndex, hIndex);
					}
				}
				else
				{
					// Split edges and move vertices into place.
					int numEdgesToCreate = (int)extrudedVertexPositions.size() - (int)(edgeData.size() - 2);
					ASSERT_TRACE(numEdgesToCreate > 0 && "Mesh::extrudePolygonEdgesNormal(): invalid offsetting.");

					SplitEdgeResult ser;
					MeshOperations::splitEdge(mesh, edgeData.front().index, numEdgesToCreate, &ser);

					uint32_t v0Index = ser.newVertexIndices[0];
					uint32_t v1Index = edgeData.back().v0;

					for (uint32_t i = 0; i < ser.newEdgeIndices.size(); ++i)
					{
						extrudedEdgeIndices->push_back(ser.newEdgeIndices[i]);
					}
					for (uint32_t i = 1; i < edgeData.size() - 1; ++i)
					{
						extrudedEdgeIndices->push_back(edgeData[i].index);
					}

					// Get vertex indices
					IndexVector vertexIndices = ser.newVertexIndices;
					for (uint32_t i = 1; i < edgeData.size(); ++i)
					{
						vertexIndices.push_back(edgeData[i].v0);
					}

					// Store old end vertices for checking collinearity
					collinearVertices[0] = edgeData.front().v0;
					collinearVertices[1] = edgeData.back().v1;

					// Store new end vertices for tapering/chamfering
					endVertices[0] = v0Index;
					endVertices[1] = v1Index;

					// No source edges
					sourceEdgeIndices->clear();

					// Move vertices
					for (uint32_t i = 0; i < vertexIndices.size(); ++i)
					{
						mesh->moveVertexTo(vertexIndices[i], extrudedVertexPositions[i]);
					}

					*newPolygonIndex = polygonIndex;
				}
			}
			else
			{
				// Create vertices
				IndexVector newVertexIndices;
				for (auto const& pos : extrudedVertexPositions)
				{
					newVertexIndices.push_back(mesh->addVertex(Vertex(pos)));
				}

				IndexVector newEdgeData;
				for (uint32_t i = 0; i < newVertexIndices.size() - 1; ++i)
				{
					uint32_t edgeIndex = mesh->addEdge(Edge(newVertexIndices[i], newVertexIndices[i + 1]));
					extrudedEdgeIndices->push_back(edgeIndex);

					newEdgeData.push_back(newVertexIndices[i]);
					newEdgeData.push_back(newVertexIndices[i + 1]);
					newEdgeData.push_back(edgeIndex);
				}

				if (isLoop)
				{
					// Extrude new polygon and add old polygon as filled in hole.
					// Join up loop.
					uint32_t edgeIndex = mesh->addEdge(Edge(newVertexIndices.back(), newVertexIndices.front()));
					extrudedEdgeIndices->push_back(edgeIndex);

					newEdgeData.push_back(newVertexIndices.back());
					newEdgeData.push_back(newVertexIndices.front());
					newEdgeData.push_back(edgeIndex);

					// No collinear vertices here
					collinearVertices[0] = (uint32_t)-1;
					collinearVertices[1] = (uint32_t)-1;

					// No end vertices in a loop
					endVertices[0] = (uint32_t)-1;
					endVertices[1] = (uint32_t)-1;

					// Source edges are those in edgeData
					for (auto const& de : edgeData)
					{
						sourceEdgeIndices->push_back(de.index);
					}

					// Create polygon
					Polygon newPolygon(newEdgeData);
					*newPolygonIndex = mesh->addPolygon(newPolygon);

					// Add old as hole and fill in.
					mesh->addFilledHoleToPolygon(*newPolygonIndex, polygonIndex, holeIndex);
				}
				else
				{
					// Store old end vertices for checking collinearity
					collinearVertices[0] = edgeData.front().v0;
					collinearVertices[1] = edgeData.back().v1;

					// Store new end vertices for tapering/chamfering
					endVertices[0] = newVertexIndices.front();
					endVertices[1] = newVertexIndices.back();

					// Source edges are those in edgeData
					for (auto const& de : edgeData)
					{
						sourceEdgeIndices->push_back(de.index);
					}

					// Create edges from new loop to old.
					Edge e0(edgeData.front().v0, newVertexIndices.front());
					Edge e1(Edge(newVertexIndices.back(), edgeData.back().v1));
					uint32_t e0Index = mesh->addEdge(e0);
					uint32_t e1Index = mesh->addEdge(e1);

					// Join up edges: new edges, then second connector, then old edges in
					// reverse, then first connector.
					newEdgeData.push_back(e1.getFirstVertex());
					newEdgeData.push_back(e1.getSecondVertex());
					newEdgeData.push_back(e1Index);

					for (auto it = edgeData.rbegin(); it != edgeData.rend(); ++it)
					{
						auto const& de = *it;
						newEdgeData.push_back(de.v1);
						newEdgeData.push_back(de.v0);
						newEdgeData.push_back(de.index);
					}

					newEdgeData.push_back(e0.getFirstVertex());
					newEdgeData.push_back(e0.getSecondVertex());
					newEdgeData.push_back(e0Index);

					Polygon newPolygon(newEdgeData);
					*newPolygonIndex = mesh->addPolygon(newPolygon);
				}
			}
		}

		void MeshOperations::extrudePolygonDirected(Mesh* mesh, uint32_t polygonIndex, IndexVector const& edgeIndices, Vector2 const& extrusion, ExtrudePolygonOptions options, ExtrudePolygonResult* result)
		{
			if (!MeshUtils::areEdgesInPolygon(mesh, polygonIndex, edgeIndices))
			{
				string errMsg = std::format("edge(s) are not in polygon {}", polygonIndex);
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			}

			// If not separate, break edge list into contiguous sections.
			vector<DirectedEdgeVector> sections = getExtrusionSections(mesh, polygonIndex, edgeIndices, options.separateExtrusions, false);
			doPolygonExtrusion(mesh, polygonIndex, sections, true, extrusion, options, result);
		}

		void MeshOperations::slicePolygon(Mesh* mesh, uint32_t polygonIndex, Vector2 const& v0, Vector2 const& v1, bool removeSliced, SlicePolygonResult* result)
		{
			// Intersect this ray with each edge.
			string errMsg;
			auto intersections = mesh->linePolygonIntersection(polygonIndex, v0, v1);
			switch (intersections.size())
			{
			case 0: // Nothing to do
				return;
			case 1: // Error
				errMsg = std::format("cannot slice polygon {} from ({},{}) to ({},{}): the line only intersects the polygon once.",
					polygonIndex, v0.x, v0.y, v1.x, v1.y);
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			case 2: // OK
				break;
			default: // Error
				errMsg = std::format("cannot slice polygon {} from ({},{}) to ({},{}): the line only intersects the polygon more than twice.",
					polygonIndex, v0.x, v0.y, v1.x, v1.y);
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			}

			// Check holes
			auto const& polygon = mesh->getPolygon(polygonIndex);
			for (uint32_t holeIndex : polygon.getHoleIndices())
			{
				auto holeIntersections = mesh->linePolygonIntersection(holeIndex, v0, v1);
				if (!holeIntersections.empty())
				{
					errMsg = std::format("cannot slice polygon {} through hole {}.", polygonIndex, holeIndex);
					throw GeometryOperationException(__FUNCTION__, errMsg, true);
				}
			}

			// Order intersections by how close the intersection point is.
			Vector2 i0 = mesh->getEdge(intersections[0].first).lerp(intersections[0].second);
			Vector2 i1 = mesh->getEdge(intersections[1].first).lerp(intersections[1].second);

			if (v0.distanceToSq(i0) > v0.distanceToSq(i1))
			{
				swap(intersections[0], intersections[1]);
			}

			SplitPolygonResult spr;
			splitPolygon(mesh,
				polygonIndex,
				intersections[0].first,
				intersections[0].second,
				intersections[1].first,
				intersections[1].second,
				&spr);

			if (removeSliced)
			{
				mesh->removePolygon(spr.newPolygonIndex);
			}

			if (result)
			{
				result->newPolygonIndex = removeSliced ? -1 : (int32_t)spr.newPolygonIndex;
				result->slicingEdgeIndices = spr.splittingEdgeIndices;
			}
		}

		void MeshOperations::slicePolygon(Mesh* mesh, uint32_t polygonIndex, uint32_t vertex1Index, uint32_t vertex2Index, bool removeSliced, SlicePolygonResult* result)
		{
			string errMsg;

			auto v0 = mesh->getVertex(vertex1Index).getPosition();
			auto v1 = mesh->getVertex(vertex2Index).getPosition();

			// Check holes
			auto const& polygon = mesh->getPolygon(polygonIndex);
			for (uint32_t holeIndex : polygon.getHoleIndices())
			{
				auto holeIntersections = mesh->linePolygonIntersection(holeIndex, v0, v1);
				if (!holeIntersections.empty())
				{
					errMsg = std::format("cannot slice polygon {} through hole {}.", polygonIndex, holeIndex);
					throw GeometryOperationException(__FUNCTION__, errMsg, true);
				}
			}

			SplitPolygonResult spr;
			splitPolygon(mesh,
				polygonIndex,
				vertex1Index,
				vertex2Index,
				&spr);

			if (removeSliced)
			{
				mesh->removePolygon(spr.newPolygonIndex);
			}

			if (result)
			{
				result->newPolygonIndex = removeSliced ? -1 : (int32_t)spr.newPolygonIndex;
				result->slicingEdgeIndices = spr.splittingEdgeIndices;
			}
		}

		void MeshOperations::cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t vertex2Index, CutPolygonResult* result)
		{
			cutPolygon(mesh, polyIndex, vertex1Index, vertex2Index, IndexVector(), result);
		}

		void MeshOperations::cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t vertex2Index, CutPolygonResult* result)
		{
			SplitEdgeResult splitResult;
			MeshOperations::splitEdge(mesh, edge1Index, edge1Amt, &splitResult);

			cutPolygon(mesh, polyIndex, splitResult.newVertexIndices.front(), vertex2Index, result);
		}

		void MeshOperations::cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t vertex2Index, IndexVector const& vertexIndices, CutPolygonResult* result)
		{
			SplitEdgeResult splitResult;
			MeshOperations::splitEdge(mesh, edge1Index, edge1Amt, &splitResult);

			cutPolygon(mesh, polyIndex, splitResult.newVertexIndices.front(), vertex2Index, vertexIndices, result);

		}

		void MeshOperations::cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t edge2Index, float edge2Amt, CutPolygonResult* result)
		{
			SplitEdgeResult splitResult;
			MeshOperations::splitEdge(mesh, edge2Index, edge2Amt, &splitResult);

			cutPolygon(mesh, polyIndex, vertex1Index, splitResult.newVertexIndices.front(), result);
		}

		void MeshOperations::cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t edge2Index, float edge2Amt, IndexVector const& vertexIndices, CutPolygonResult* result)
		{
			SplitEdgeResult splitResult;
			MeshOperations::splitEdge(mesh, edge2Index, edge2Amt, &splitResult);

			cutPolygon(mesh, polyIndex, vertex1Index, splitResult.newVertexIndices.front(), vertexIndices, result);
		}

		void MeshOperations::cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t edge2Index, float edge2Amt, CutPolygonResult* result)
		{
			SplitEdgeResult split1Result, split2Result;

			MeshOperations::splitEdge(mesh, edge1Index, edge1Amt, &split1Result);
			MeshOperations::splitEdge(mesh, edge2Index, edge2Amt, &split2Result);

			cutPolygon(mesh, polyIndex, split1Result.newVertexIndices.front(), split2Result.newVertexIndices.front(), result);
		}

		void MeshOperations::cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t edge2Index, float edge2Amt, IndexVector const& vertexIndices, CutPolygonResult* result)
		{
			SplitEdgeResult split1Result, split2Result;

			MeshOperations::splitEdge(mesh, edge1Index, edge1Amt, &split1Result);
			MeshOperations::splitEdge(mesh, edge2Index, edge2Amt, &split2Result);

			cutPolygon(mesh, polyIndex, split1Result.newVertexIndices.front(), split2Result.newVertexIndices.front(), vertexIndices, result);
		}

		void MeshOperations::cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t vertex2Index, IndexVector const& vertexIndices, CutPolygonResult* result)
		{
			// Check shared edges
			auto const& v1e = mesh->getVertex(vertex1Index).getEdgeReferences();
			auto const& v2e = mesh->getVertex(vertex2Index).getEdgeReferences();

			IndexVector sharedEdgeIndices;
			set_intersection(v1e.begin(), v1e.end(), v2e.begin(), v2e.end(), back_inserter(sharedEdgeIndices));

			if (!sharedEdgeIndices.empty())
			{
				string errMsg = std::format("vertex {} and vertex {} share an edge.", vertex1Index, vertex2Index);
				throw GeometryOperationInvalidArgument(__FUNCTION__, "vertex1Index|vertex2Index", errMsg);
			}

			// Cut the polygon
			auto& polygon = mesh->_getPolygon(polyIndex);

			// Store holes for now, so we can work out which have been reassigned
			IndexList oldHoleIndices = polygon.getHoleIndices();

			IndexVector newEdgeIndices;
			DirectedEdgeVector removedEdges;
			polygon.cut(vertex1Index, vertex2Index, vertexIndices, &newEdgeIndices, &removedEdges);

			// Reassign holes: anything in 'old' which not in 'new'.
			IndexList removedHoleIndices;
			auto const& newHoleIndices = mesh->getPolygon(polyIndex).getHoleIndices();
			set_difference(oldHoleIndices.begin(), oldHoleIndices.end(), newHoleIndices.begin(), newHoleIndices.end(), back_inserter(removedHoleIndices));

			for (uint32_t holeIndex : removedHoleIndices)
			{
				mesh->removeHoleFromPolygon(polyIndex, holeIndex);
			}

			if (result)
			{
				result->cuttingEdgeIndices = newEdgeIndices;
				result->edgesRemoved = removedEdges;
				copy(removedHoleIndices.begin(), removedHoleIndices.end(), back_inserter(result->holesRemovedIndices));
			}
		}

		void MeshOperations::mergePolygonsByEdge(Mesh* mesh, uint32_t poly1Index, uint32_t poly2Index, float vertexTolerance, MergePolygonsResult* result)
		{
			// Get all shared edges
			auto edgesIndices1 = mesh->getPolygon(poly1Index).getEdgeIndexSet();
			auto edgesIndices2 = mesh->getPolygon(poly2Index).getEdgeIndexSet();

			IndexVector edgeIndices;
			set_intersection(edgesIndices1.begin(), edgesIndices1.end(), edgesIndices2.begin(), edgesIndices2.end(), back_inserter(edgeIndices));

			// Get vertices of the edges to check for collinearity later
			IndexSet collinearVertices;
			for (uint32_t edgeIndex : edgeIndices)
			{
				auto const& edge = mesh->getEdge(edgeIndex);
				collinearVertices.insert(edge.getFirstVertex());
				collinearVertices.insert(edge.getSecondVertex());
			}

			// Get edge lists for polygons
			DirectedEdgeVector edges1 = mesh->getPolygon(poly1Index).getEdges();
			DirectedEdgeVector edges2 = mesh->getPolygon(poly2Index).getEdges();

			// Get the shared edges in order
			IndexVector orderedSharedEdges;
			for (auto const de : edges1)
			{
				if (find_if(edgeIndices.begin(), edgeIndices.end(), [de](uint32_t edgeIndex)
				{
					return edgeIndex == de.index;
				}) != edgeIndices.end())
				{
					orderedSharedEdges.push_back(de.index);
				}
			}

			// Rotate the edge lists to line them up.
			rotate(edges1.begin(), find_if(edges1.begin(), edges1.end(), [orderedSharedEdges](auto const& de)
			{
				return orderedSharedEdges.front() == de.index;
			}), edges1.end());

			rotate(edges2.begin(), find_if(edges2.begin(), edges2.end(), [orderedSharedEdges](auto const& de)
			{
				return orderedSharedEdges.front() == de.index;
			}), edges2.end());

			// Split first list by the delimiting edges
			list<DirectedEdgeVector> subEdgeLists1 = MeshUtils::splitDirectedEdgeVector(edges1, orderedSharedEdges);

			if (edges1.front().index != orderedSharedEdges.front())
			{
				auto front = subEdgeLists1.front();
				auto& back = subEdgeLists1.back();
				subEdgeLists1.pop_front();
				copy(front.begin(), front.end(), back_inserter(back));
			}

			// Split second list by the delimiting edges
			IndexVector reversedOrderedSharedEdges = orderedSharedEdges;
			reverse(reversedOrderedSharedEdges.begin(), reversedOrderedSharedEdges.end());
			rotate(reversedOrderedSharedEdges.begin(), --reversedOrderedSharedEdges.end(), reversedOrderedSharedEdges.end());

			list<DirectedEdgeVector> subEdgeLists2 = MeshUtils::splitDirectedEdgeVector(edges2, reversedOrderedSharedEdges);

			// Update polygon and deal with new holes
			DirectedEdgeVector addEdges;
			IndexVector holeIndices;
			IndexSet keepEdgeIndices;

			auto it2 = subEdgeLists2.rbegin();
			for (auto it1 = subEdgeLists1.begin(); it1 != subEdgeLists1.end(); ++it1, ++it2)
			{
				auto edgeList1 = *it1;
				auto edgeList1Copy = edgeList1;
				auto const& edgeList2 = *it2;
				copy(edgeList2.begin(), edgeList2.end(), back_inserter(edgeList1));

				// If anticlockwise, this is the polygon.  If clockwise, it is a new hole.
				vector<Vector2> polygonVertices;
				for (auto const& edge : edgeList1)
				{
					polygonVertices.push_back(mesh->getVertex(edge.v0).getPosition());
				}

				// We will have one polygon with anti-clockwise winding, which will be the new
				// polygon, and zero or more with clockwise, which will be new holes in it.
				// We must construct the new polygon in such a way that edges and vertices that
				// the hole(s) might use are not deleted.
				Winding polygonWinding = MathsUtils::pointsWinding(polygonVertices);

				if (polygonWinding == Winding::Anticlockwise)
				{
					for (auto const& edge : edgeList1Copy)
					{
						keepEdgeIndices.insert(edge.index);
					}

					copy(edgeList2.begin(), edgeList2.end(), back_inserter(addEdges));
				}
				else
				{
					IndexVector holeEdgeData;
					for (auto const& edge : edgeList1)
					{
						holeEdgeData.push_back(edge.v0);
						holeEdgeData.push_back(edge.v1);
						holeEdgeData.push_back(edge.index);
					}

					Polygon hole(holeEdgeData);
					holeIndices.push_back(mesh->addPolygon(hole));
				}
			}

			// Get modifiable polygons
			auto& polygon1 = mesh->_getPolygon(poly1Index);
			auto& polygon2 = mesh->_getPolygon(poly2Index);

			// Remove edges from poly1 not in edgeList1
			polygon1.removeEdgesNotInSet(keepEdgeIndices);

			// Add edges to poly1 from edgeList2
			size_t numEdges = addEdges.size();
			for (size_t i = 0; i < numEdges; ++i)
			{
				auto addedEdgeIt = polygon1.addEdge(addEdges[i].v0, addEdges[i].v1, addEdges[i].index);
			}

			// Add holes
			for (uint32_t holeIndex : holeIndices)
			{
				mesh->addHoleToPolygon(poly1Index, holeIndex);
			}

			// Transfer holes from polygon2 to polygon1
			for (uint32_t holeIndex : polygon2.getHoleIndices())
			{
				mesh->addHoleToPolygon(poly1Index, holeIndex);
			}

			polygon2.mHoleIndices.clear();

			// Update grid for poly1
			if (mesh->mPolygonAccelerationGrid)
			{
				mesh->mPolygonAccelerationGrid->moveItem(poly1Index, polygon1.getBoundingBox());
			}

			// Delete poly2 and remove from grid
			mesh->removePolygon(poly2Index);

			// Remove vertices if they're not used.
			if (vertexTolerance > 0.0f)
			{
				for (uint32_t vertexIndex : collinearVertices)
				{
					if (mesh->isVertexCollinearOrphan(vertexIndex, vertexTolerance))
					{
						try
						{
							mesh->removeVertex(vertexIndex);
						}
						catch (GeometryOperationException& e)
						{
							throw e;
						}

						if (result)
						{
							result->verticesRemoved.push_back(vertexIndex);
						}
					}
				}
			}

			if (result)
			{
				result->edgesRemoved = edgeIndices;
				result->oldIndices[0] = poly1Index;
				result->oldIndices[1] = poly2Index;
				result->newIndex = poly1Index;
			}
		}

	} // geometry
} // WP_NAMESPACE
