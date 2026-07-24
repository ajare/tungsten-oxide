#include <algorithm>

#include "willpower/common/MathsUtils.h"
#include "willpower/common/WillpowerWalker.h"

#include "willpower/geometry/MeshUtils.h"
#include "willpower/geometry/Mesh.h"
#include "willpower/geometry/clipper.hpp"

#undef max

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace std;
		using namespace WP_NAMESPACE;

		MeshUtils::OrderedEdgeGroupSet MeshUtils::groupConnectedEdges(list<MeshUtils::EdgeIndexInfo> const& edges, bool allowFlips)
		{
			EdgeInfoList currentEdges, edgeList = edges;
			OrderedEdgeGroupSet groups;

			if (edges.empty())
			{
				return groups;
			}

			EdgeIndexInfo initialEdge = edgeList.back();

			edgeList.pop_back();
			currentEdges.push_back(initialEdge);

			while (!edgeList.empty())
			{
				// Find the edge which matches up with the first vertex in the list
				auto prevEdgeIt = find_if(edgeList.begin(), edgeList.end(), [currentEdges](EdgeIndexInfo const& edge)
				{
					return get<1>(currentEdges.front()) == get<2>(edge);
				});

				// Find the edge which matches up with the last vertex in the list
				auto nextEdgeIt = find_if(edgeList.begin(), edgeList.end(), [currentEdges](EdgeIndexInfo const& edge)
				{
					return get<2>(currentEdges.back()) == get<1>(edge);
				});

				// If prevEdgeIt or nextEdgeIt are not found and we are allowing flips, then
				// search for flipped edges
				bool flipPrev = false, flipNext = false;
				if (prevEdgeIt == edgeList.end() && allowFlips)
				{
					prevEdgeIt = find_if(edgeList.begin(), edgeList.end(), [currentEdges](EdgeIndexInfo const& edge)
					{
						return get<1>(currentEdges.front()) == get<1>(edge);
					});

					if (prevEdgeIt != edgeList.end())
					{
						flipPrev = true;
					}
				}

				if (nextEdgeIt == edgeList.end() && allowFlips)
				{
					nextEdgeIt = find_if(edgeList.begin(), edgeList.end(), [currentEdges](EdgeIndexInfo const& edge)
					{
						return get<2>(currentEdges.back()) == get<2>(edge);
					});

					if (nextEdgeIt != edgeList.end())
					{
						flipNext = true;
					}
				}

				bool addedEdge = false;

				// Check for loops with an even number of edges
				bool closedLoop = prevEdgeIt == nextEdgeIt;

				// Add the previous edge, if we found one
				if (prevEdgeIt != edgeList.end())
				{
					if (flipPrev)
					{
						auto flippedEdge = make_tuple(get<0>(*prevEdgeIt), get<2>(*prevEdgeIt), get<1>(*prevEdgeIt));
						currentEdges.push_front(flippedEdge);
					}
					else
					{
						currentEdges.push_front(*prevEdgeIt);
					}

					edgeList.erase(prevEdgeIt);
					addedEdge = true;
				}

				// Add the next edge, if we found one, making sure that we don't add it if we found the same edge (ie closed a loop)
				if (!closedLoop && nextEdgeIt != edgeList.end())
				{
					if (flipNext)
					{
						auto flippedEdge = make_tuple(get<0>(*nextEdgeIt), get<2>(*nextEdgeIt), get<1>(*nextEdgeIt));
						currentEdges.push_front(flippedEdge);
					}
					else
					{
						currentEdges.push_back(*nextEdgeIt);
					}

					edgeList.erase(nextEdgeIt);
					addedEdge = true;
				}

				if (!addedEdge)
				{
					// Didn't find any edges to add, so we must have found a full group
					EdgeInfoVector group(currentEdges.begin(), currentEdges.end());
					groups.push_back(group);

					// Re-seed group
					currentEdges.clear();

					initialEdge = edgeList.back();

					edgeList.pop_back();
					currentEdges.push_back(initialEdge);
				}
			}

			// Add final group
			EdgeInfoVector group(currentEdges.begin(), currentEdges.end());
			groups.push_back(group);

			return groups;
		}

		MeshUtils::OrderedEdgeGroupSet MeshUtils::groupConnectedEdges(Mesh const* mesh, uint32_t polygonIndex, IndexVector const& edgeIndices, bool allowFlips)
		{
			auto const& polygon = mesh->getPolygon(polygonIndex);

			list<MeshUtils::EdgeIndexInfo> edgeInfo;
			for (uint32_t edgeIndex: edgeIndices)
			{
				auto deIt = polygon.getEdgeByIndex(edgeIndex);
				edgeInfo.push_back(make_tuple(deIt->index, deIt->v0, deIt->v1));
			}

			return groupConnectedEdges(edgeInfo, allowFlips);
		}

		vector<IndexVector> MeshUtils::groupConnectedPolygons(Mesh const* mesh)
		{
			// Working data structure
			struct PolygonInfo
			{
				int sector;
				IndexSet neighbours;
			};

			vector<PolygonInfo> polygons;

			// Get polygon neighbour information
			for (uint32_t i = 0; i < mesh->getNumPolygons(); ++i)
			{
				auto const& polygon = mesh->getPolygon(i);

				PolygonInfo pi;

				pi.sector = -1;

				for (auto it = polygon.getFirstEdge(); it != polygon.getEndEdge(); ++it)
				{
					Edge const& edge = mesh->getEdge((*it).index);
					
					for (auto polyRef: edge.getPolygonReferences())
					{
						if (polyRef != i)
						{
							pi.neighbours.insert(polyRef);
						}
					}
				}

				polygons.push_back(pi);
			}

			// Build groups
			map<uint32_t, IndexList> groups;
			vector<IndexVector> connectedGroups;

			// Add first polygon
			groups[0] = IndexList();
			groups[0].push_back(0);
			polygons[0].sector = 0;

			int sectorCount = 1;

			for (uint32_t i = 1; i < polygons.size(); ++i)
			{
				IndexSet neighbours;
				for (auto neighbour: polygons[i].neighbours)
				{
					if (polygons[neighbour].sector >= 0)
					{
						neighbours.insert(polygons[neighbour].sector);
					}
				}

				switch (neighbours.size())
				{
				case 0:
					// Add to new
					groups[sectorCount] = IndexList();
					groups[sectorCount].push_back(i);
					polygons[i].sector = sectorCount;
					sectorCount++;
					break;
				case 1:
					// Add to existing
					groups[*neighbours.begin()].push_back(i);
					polygons[i].sector = *neighbours.begin();
					break;
				default:
					// Set the polygon in question, and then merge
					polygons[i].sector = *neighbours.begin();
					groups[polygons[i].sector].push_back(i);

					for (auto it = ++neighbours.begin(); it != neighbours.end(); ++it)
					{
						auto& target = groups[polygons[i].sector];
						auto const& source = groups[*it];

						// Update sector ids
						for (auto id: source)
						{
							polygons[id].sector = polygons[i].sector;
						}

						// Copy over
						copy(source.begin(), source.end(), back_inserter(target));

						// Erase group
						groups.erase(*it);
					}
					break;
				}
			}

			// Build lists
			for (auto groupIt: groups)
			{
				auto const& groupList = groupIt.second;

				IndexVector connectedGroup;
				copy(groupList.begin(), groupList.end(), back_inserter(connectedGroup));

				connectedGroups.push_back(connectedGroup);
			}

			return connectedGroups;
		}

		uint32_t MeshUtils::getNearestEdgeIndex(Mesh const* mesh, Vector2 const& point, IndexSet const& edgeIndices)
		{
			float curEdgeDistance = numeric_limits<float>::max();
			uint32_t nearestEdge = 0;

			for (uint32_t edgeIndex: edgeIndices)
			{
				Edge const& edge = mesh->getEdge(edgeIndex);

				float dist = edge.getDistanceTo(point);
				if (dist < curEdgeDistance)
				{
					curEdgeDistance = dist;
					nearestEdge = edgeIndex;
				}
			}

			return nearestEdge;
		}

		float MeshUtils::anticlockwiseAngleBetweenConnectedEdges(Mesh const* mesh, uint32_t edge0Index, uint32_t edge1Index)
		{
			auto const& edge0 = mesh->getEdge(edge0Index);
			auto const& edge1 = mesh->getEdge(edge1Index);

			ASSERT_TRACE(edge0.getSecondVertex() == edge1.getFirstVertex() && "MeshUtils::anticlockwiseAngleBetweenEdges() edges are not connected");

			wp::Vector2 v0, v1, v2;
			v0 = mesh->getVertex(edge0.getFirstVertex()).getPosition();
			v1 = mesh->getVertex(edge0.getSecondVertex()).getPosition();
			v2 = mesh->getVertex(edge1.getSecondVertex()).getPosition();

			return MathsUtils::anticlockwiseAngleBetween(v0, v1, v2);
		}

		float MeshUtils::clockwiseAngleBetweenConnectedEdges(Mesh const* mesh, uint32_t edge0Index, uint32_t edge1Index)
		{
			auto const& edge0 = mesh->getEdge(edge0Index);
			auto const& edge1 = mesh->getEdge(edge1Index);

			ASSERT_TRACE(edge0.getSecondVertex() == edge1.getFirstVertex() && "MeshUtils::anticlockwiseAngleBetweenEdges() edges are not connected");

			wp::Vector2 v0, v1, v2;
			v0 = mesh->getVertex(edge0.getFirstVertex()).getPosition();
			v1 = mesh->getVertex(edge0.getSecondVertex()).getPosition();
			v2 = mesh->getVertex(edge1.getSecondVertex()).getPosition();

			return MathsUtils::clockwiseAngleBetween(v0, v1, v2);
		}

		Winding MeshUtils::getVertexWinding(vector<Vector2> const& vertices)
		{
			float sum = 0.0f;

			for (size_t i = 0; i < vertices.size(); ++i)
			{
				auto const& p0 = vertices[i];
				auto const& p1 = vertices[(i == vertices.size() - 1) ? 0 : i + 1];

				sum += (p1.x - p0.x) * (p1.y + p0.y);
			}

			return sum > 0 ? Winding::Clockwise : Winding::Anticlockwise;
		}

		vector<vector<Vector2>> MeshUtils::insetVertexLoop(vector<Vector2> const& vertices, float distance, bool rounded)
		{
			vector<vector<Vector2>> insetVertices;

			ClipperLib::ClipperOffset clipperOffset;
			ClipperLib::Path inPath;
			ClipperLib::Paths outPaths;

			// Build paths for Clipper
			for (auto const& vertex: vertices)
			{
				inPath.push_back(ClipperLib::IntPoint((int)vertex.x, (int)vertex.y));
			}

			// Inset
			clipperOffset.AddPath(inPath, rounded ? ClipperLib::JoinType::jtRound : ClipperLib::JoinType::jtMiter, ClipperLib::EndType::etClosedPolygon);
			clipperOffset.Execute(outPaths, -distance);

			// Build return vertices
			for (auto const& path: outPaths)
			{
				vector<Vector2> outVertices;

				for (auto const& point: path)
				{
					outVertices.push_back(Vector2((float)point.X, (float)point.Y));
				}

				insetVertices.push_back(outVertices);
			}

			return insetVertices;
		}

		vector<vector<Vector2>> MeshUtils::insetVertexLoops(vector<vector<Vector2>> const& loops, float distance, bool rounded)
		{
			vector<vector<Vector2>> insetVertices;

			ClipperLib::ClipperOffset clipperOffset;
			ClipperLib::Paths inPaths, outPaths;

			// Build paths for Clipper
			for (auto const& loop: loops)
			{
				ClipperLib::Path inPath;

				for (auto const& vertex : loop)
				{
					inPath.push_back(ClipperLib::IntPoint((int)vertex.x, (int)vertex.y));
				}

				inPaths.push_back(inPath);
			}

			// Inset
			clipperOffset.AddPaths(inPaths, rounded ? ClipperLib::JoinType::jtRound : ClipperLib::JoinType::jtMiter, ClipperLib::EndType::etClosedPolygon);
			clipperOffset.Execute(outPaths, -distance);

			// Build return vertices
			for (auto const& path: outPaths)
			{
				vector<Vector2> vertices;

				for (auto const& point: path)
				{
					vertices.push_back(Vector2((float)point.X, (float)point.Y));
				}

				insetVertices.push_back(vertices);
			}

			return insetVertices;
		}

		MeshUtils::EdgeInfoList MeshUtils::getEdgeInfo(IndexVector const& edgeIndices, Mesh* mesh)
		{
			EdgeInfoList edgeInfo;

			for (uint32_t edgeIndex: edgeIndices)
			{
				auto const& edge = mesh->getEdge(edgeIndex);

				edgeInfo.push_back(make_tuple(
					edgeIndex,
					edge.getFirstVertex(),
					edge.getSecondVertex()));
			}
			
			return edgeInfo;
		}
		
		list<DirectedEdgeVector> MeshUtils::splitDirectedEdgeVector(DirectedEdgeVector const& edges, IndexVector const& delims)
		{
			DirectedEdgeVector subEdgeList;
			list<DirectedEdgeVector> subEdgeLists;
			
			int sharedIndex = 0;
			for (auto const& edge: edges)
			{
				if ((size_t)sharedIndex < delims.size() && edge.index == delims[sharedIndex])
				{
					if (!subEdgeList.empty())
					{
						subEdgeLists.push_back(subEdgeList);
						subEdgeList.clear();
					}
					sharedIndex++;
				}
				else
				{
					subEdgeList.push_back(edge);
				}
			}

			if (!subEdgeList.empty())
			{
				subEdgeLists.push_back(subEdgeList);
			}

			return subEdgeLists;
		}

		IndexSet MeshUtils::getPolygonReferences(Mesh const* mesh, IndexVector const& edgeIndices)
		{
			IndexSet result;

			for (uint32_t edgeIndex: edgeIndices)
			{
				auto const& polygonRefs = mesh->getEdge(edgeIndex).getPolygonReferences();
				set_union(result.begin(), result.end(), polygonRefs.begin(), polygonRefs.end(), inserter(result, result.end()));
			}

			return result;
		}

		Vector2 MeshUtils::calculateEdgeListCentre(Mesh const* mesh, EdgeInfoVector const& edgeInfo, uint32_t* edgeIndexResult)
		{
			float totalLength = 0.0f;
			for (auto edgeData: edgeInfo)
			{
				uint32_t edgeIndex = get<0>(edgeData);
				totalLength += mesh->getEdge(edgeIndex).getLength();
			}

			float edgeCentre = totalLength / 2.0f;

			totalLength = 0.0f;
			for (auto edgeData: edgeInfo)
			{
				uint32_t thisEdgeIndex = get<0>(edgeData);
				auto const& edge = mesh->getEdge(thisEdgeIndex);
				float edgeLength = edge.getLength();
				if (edgeCentre >= totalLength && edgeCentre < (totalLength + edgeLength))
				{
					if (edgeIndexResult)
					{
						*edgeIndexResult = thisEdgeIndex;
					}

					return edge.lerp((edgeCentre - totalLength) / edgeLength);
				}

				totalLength += edgeLength;
			}

#pragma warning(suppress: 4127)
			ASSERT_TRACE(false && "MeshUtils::calculateEdgeListCentre() edge centre not found.");
			return Vector2::ZERO;
		}

		bool MeshUtils::areEdgesInPolygon(Mesh const* mesh, uint32_t polygonIndex, IndexVector const& edgeIndices)
		{
			// Check edges in polygon
			auto const& polygon = mesh->getPolygon(polygonIndex);
			auto const& polygonEdgeIndices = polygon.getEdgeIndexSet();

			for (uint32_t edgeIndex : edgeIndices)
			{
				if (polygonEdgeIndices.find(edgeIndex) == polygonEdgeIndices.end())
				{
					return false;
				}
			}

			return true;
		}

	} // geometry
} // WP_NAMESPACE
