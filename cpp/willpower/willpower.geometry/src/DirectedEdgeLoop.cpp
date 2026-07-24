#include <map>
#include <cassert>
#include <algorithm>

#include <willpower/common/StringUtils.h>

#include "willpower/common/WillpowerWalker.h"

#include "willpower/geometry/DirectedEdgeLoop.h"
#include "willpower/geometry/MeshUtils.h"
#include "willpower/geometry/Mesh.h"
#include "willpower/geometry/Exception.h"
#include "willpower/geometry/TypeConverters.h"

#undef max

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace std;
		using namespace utils;

		DirectedEdgeLoop::DirectedEdgeLoop(Winding winding, IndexVector const& edgeData)
			: mwMesh(nullptr)
			, mMeshIndex(-1)
			, mWinding(winding)
			, mDeleteFunction({})
			, mOrderedVertexIndicesCached(false)
			, mOrderedVertexIndicesWinding(Winding::Anticlockwise)
			, mUpdateEdgesFunction({})

		{
			mOrderedVertexIndicesBreakIndices.first = -1;
			mOrderedVertexIndicesBreakIndices.second = -1;

			for (uint32_t i = 0; i < edgeData.size(); i += 3)
			{
				insertEdge(edgeData[i + 0], edgeData[i + 1], edgeData[i + 2], mEdges.end());
			}
		}

		DirectedEdgeLoop::DirectedEdgeLoop(DirectedEdgeLoop const& other)
		{
			copyFrom(other);
		}

		DirectedEdgeLoop& DirectedEdgeLoop::operator=(DirectedEdgeLoop const& other)
		{
			copyFrom(other);
			return *this;
		}

		void DirectedEdgeLoop::copyFrom(DirectedEdgeLoop const& other)
		{
			mwMesh = other.mwMesh;
			mMeshIndex = other.mMeshIndex;

			mWinding = other.mWinding;
			mOrderedVertexIndicesCached = other.mOrderedVertexIndicesCached;
			mOrderedVertexIndices = other.mOrderedVertexIndices;
			mOrderedVertexIndicesBreakIndices = other.mOrderedVertexIndicesBreakIndices;
			mOrderedVertexIndicesWinding = other.mOrderedVertexIndicesWinding;
			mVertexAttributeIndices = other.mVertexAttributeIndices;

			// Copy callbacks
			mUpdateEdgesFunction = other.mUpdateEdgesFunction;
			mDeleteFunction = other.mDeleteFunction;

			// Copy edges
			mEdges.clear();

			for (auto it = other.getFirstEdge(); it != other.getEndEdge(); ++it)
			{
				auto const& edge = *it;
				mEdges.push_back(edge);
			}
		}

		void DirectedEdgeLoop::setMesh(Mesh* mesh, int32_t index)
		{
			mwMesh = mesh;
			mMeshIndex = index;
		}

		void DirectedEdgeLoop::setUpdateRefFunction(UpdateRefFunction func)
		{
			mUpdateEdgesFunction = func;
		}

		void DirectedEdgeLoop::setDeleteFunction(DeleteFunction func)
		{
			mDeleteFunction = func;
		}

		void DirectedEdgeLoop::setWinding(Winding winding)
		{
			mWinding = winding;
			reorderEdges(mwMesh);
		}

		Winding DirectedEdgeLoop::getWinding() const
		{
			return mWinding;
		}

		int DirectedEdgeLoop::getNumEdges() const
		{
			return (int)mEdges.size();
		}

		Mesh* DirectedEdgeLoop::getMesh()
		{
			return mwMesh;
		}

		int32_t DirectedEdgeLoop::getMeshIndex() const
		{
			return mMeshIndex;
		}

		void DirectedEdgeLoop::invalidateOrderedVertexCache()
		{
			mOrderedVertexIndicesCached = false;
			invalidateEdgeData();
		}

		IndexVector DirectedEdgeLoop::getOrderedVertexIndices(pair<int, int>* breakIndices) const
		{
			return getCachedOrderedVertexIndices(breakIndices);
		}

		IndexVector const& DirectedEdgeLoop::getCachedOrderedVertexIndices(pair<int, int>* breakIndices) const
		{
			if (mOrderedVertexIndicesCached)
			{
				if (breakIndices)
				{
					*breakIndices = mOrderedVertexIndicesBreakIndices;
				}

				return mOrderedVertexIndices;
			}

			// Not cached, so rebuild.
			mOrderedVertexIndices.clear();
			mOrderedVertexIndicesBreakIndices.first = mOrderedVertexIndicesBreakIndices.second = -1;
			mOrderedVertexIndicesWinding = mWinding;

			// Process edges into usable format.
			list<MeshUtils::EdgeIndexInfo> edges;

			for (auto const& edge: mEdges)
			{
				edges.push_back(make_tuple((uint32_t)edge.index, (uint32_t)edge.v0, (uint32_t)edge.v1));
			}

			// Domino sort them.
			vector<vector<MeshUtils::EdgeIndexInfo>> edgeGroups = MeshUtils::groupConnectedEdges(edges);

			// Check for breaks: there should be at most one group
			ASSERT_TRACE(edgeGroups.size() == 1 && "DirectedEdgeLoop::getCachedOrderedVertexIndices(): more than one break encountered in polygon.  This cannot be resolved.");

			auto const& group = edgeGroups.front();

			uint32_t front = get<1>(group.front());
			uint32_t back = get<2>(group.back());
			if (front != back)
			{
				mOrderedVertexIndicesBreakIndices.first = back;
				mOrderedVertexIndicesBreakIndices.second = front;
			}

			// Now extract vertices
			for (uint32_t i = 0; i < group.size(); ++i)
			{
				mOrderedVertexIndices.push_back(get<1>(group[i]));
				if (i == (group.size() - 1))
				{
					if (get<1>(group[0]) != get<2>(group[i]))
					{
						mOrderedVertexIndices.push_back(get<2>(group[i]));
					}
				}
			}

			// Return cached results
			mOrderedVertexIndicesCached = true;

			if (breakIndices)
			{
				*breakIndices = mOrderedVertexIndicesBreakIndices;
			}

			return mOrderedVertexIndices;
		}

		DirectedEdgeIterator DirectedEdgeLoop::addEdge(uint32_t firstVertex, uint32_t secondVertex, uint32_t edgeIndex)
		{
			if (mwMesh)
			{
				// Find the correct place to insert the edge, so that they join up.
				for (auto it = mEdges.begin(); it != mEdges.end(); ++it)
				{
					if ((*it).v1 == firstVertex)
					{
						return insertEdge(firstVertex, secondVertex, edgeIndex, ++it);
					}
					if ((*it).v0 == secondVertex)
					{

						return insertEdge(firstVertex, secondVertex, edgeIndex, it);
					}
				}

				string errMsg = std::format("cannot insert edge {} into polygon {} because its vertices were not found.",
					edgeIndex, mMeshIndex);
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			}
			else
			{
				return insertEdge(firstVertex, secondVertex, edgeIndex, mEdges.end());
			}
		}

		DirectedEdgeIterator DirectedEdgeLoop::insertEdge(uint32_t firstVertex, uint32_t secondVertex, uint32_t edgeIndex, DirectedEdgeIterator where)
		{
			DirectedEdge edge;

			edge.v0 = firstVertex;
			edge.v1 = secondVertex;
			edge.index = edgeIndex;

			auto inserted = mEdges.insert(where, edge);

			// Update edge refs
			if (mUpdateEdgesFunction)
			{
				mUpdateEdgesFunction(mwMesh, mMeshIndex, -1, edgeIndex, true);
			}

			invalidateOrderedVertexCache();
			return inserted;
		}

		DirectedEdgeIterator DirectedEdgeLoop::insertEdge(uint32_t firstVertex, uint32_t secondVertex, uint32_t edgeIndex)
		{
			return insertEdge(firstVertex, secondVertex, edgeIndex, mEdges.end());
		}
		
		void DirectedEdgeLoop::updateEdge(DirectedEdgeIterator edge, uint32_t firstVertex, uint32_t secondVertex, uint32_t edgeIndex)
		{
			DirectedEdge const& e = *edge;
			DirectedEdge& e2 = const_cast<DirectedEdge&>(e);

			// Update edge refs
			if (mUpdateEdgesFunction)
			{
				mUpdateEdgesFunction(mwMesh, mMeshIndex, e2.index, edgeIndex, true);
			}

			e2.v0 = firstVertex;
			e2.v1 = secondVertex;
			e2.index = edgeIndex;
			
			invalidateOrderedVertexCache();
		}

		void DirectedEdgeLoop::updateEdgeFirstVertex(DirectedEdgeIterator edge, uint32_t vertexIndex)
		{
			DirectedEdge const& e = *edge;
			DirectedEdge& e2 = const_cast<DirectedEdge&>(e);

			e2.v0 = vertexIndex;

			invalidateEdgeData();
		}

		void DirectedEdgeLoop::updateEdgeSecondVertex(DirectedEdgeIterator edge, uint32_t vertexIndex)
		{
			DirectedEdge const& e = *edge;
			DirectedEdge& e2 = const_cast<DirectedEdge&>(e);

			e2.v1 = vertexIndex;

			invalidateEdgeData();
		}
		
		void DirectedEdgeLoop::removeEdge(uint32_t edgeIndex, bool ignoreIfNotPresent)
		{
			// Remove the edge
			bool foundEdge = false;
			for (auto it = mEdges.begin(); it != mEdges.end();)
			{
				if ((*it).index == edgeIndex)
				{
					auto it2 = it++;

					mEdges.erase(it2);
					foundEdge = true;

					// Update edge refs
					if (mUpdateEdgesFunction)
					{
						mUpdateEdgesFunction(mwMesh, mMeshIndex, edgeIndex, -1, true);
					}

					if (mEdges.empty())
					{
						mDeleteFunction(mwMesh, mMeshIndex);
					}
					else
					{
						invalidateOrderedVertexCache();
					}

					break;
				}
				else
				{
					it++;
				}
			}

			if (!foundEdge && !ignoreIfNotPresent)
			{
				string errMsg = std::format("edge {} is not a part of polygon {}", edgeIndex, mMeshIndex);
				throw GeometryOperationException(__FUNCTION__, errMsg, false);
			}
		}

		void DirectedEdgeLoop::replaceEdge(uint32_t edgeIndex, uint32_t newEdgeIndex, uint32_t vertex0Index, uint32_t vertex1Index, bool fixAdjacent, bool ignoreIfNotPresent)
		{
			// Remove the edge
			bool foundEdge = false;
			for (auto it = mEdges.begin(); it != mEdges.end();)
			{
				if ((*it).index == edgeIndex)
				{
					auto it2 = it++;
					
					(*it2).index = newEdgeIndex;
					(*it2).v0 = vertex0Index;
					(*it2).v1 = vertex1Index;

					foundEdge = true;

					// Update edge refs
					if (mUpdateEdgesFunction)
					{
						mUpdateEdgesFunction(mwMesh, mMeshIndex, edgeIndex, -1, true);
						mUpdateEdgesFunction(mwMesh, mMeshIndex, -1, newEdgeIndex, true);
					}

					if (fixAdjacent)
					{
						// Prev
						if (it2 == mEdges.begin())
						{
							mEdges.back().v1 = vertex0Index;
						}
						else
						{
							(*--it2).v1 = vertex0Index;
						}

						// Next
						if (it == mEdges.end())
						{
							mEdges.front().v0 = vertex1Index;
						}
						else
						{
							(*it).v0 = vertex1Index;
						}
					}

					invalidateOrderedVertexCache();
					break;
				}
				else
				{
					it++;
				}
			}

			if (!foundEdge && !ignoreIfNotPresent)
			{
				string errMsg = std::format("edge {} is not a part of polygon {}.", edgeIndex, mMeshIndex);
				throw GeometryOperationException(__FUNCTION__, errMsg, false);
			}
		}

		void DirectedEdgeLoop::removeEdgesNotInSet(IndexSet const& edgeIndices)
		{
			for (auto it = mEdges.begin(); it != mEdges.end();)
			{
				uint32_t edgeIndex = (*it).index;
				if (edgeIndices.find(edgeIndex) == edgeIndices.end())
				{
					auto it2 = it++;
					mEdges.erase(it2);

					// Update edge refs
					if (mUpdateEdgesFunction)
					{
						mUpdateEdgesFunction(mwMesh, mMeshIndex, edgeIndex, -1, true);
					}

					if (mEdges.empty())
					{
						mDeleteFunction(mwMesh, mMeshIndex);
						return;
					}
					else
					{
						invalidateOrderedVertexCache();
					}
				}
				else
				{
					it++;
				}
			}
		}

		void DirectedEdgeLoop::removeTwoSidedEdges()
		{
			// Sort edges by index, and then wherever there are two consecutive edges with the same
			// index, remove them.
			mEdges.sort([](DirectedEdge const& a, DirectedEdge const& b) -> bool
			{
				return a.index < b.index;
			});

			auto prevIt = mEdges.end();
			for (auto it = mEdges.begin(); it != mEdges.end();)
			{
				int curEdgeIndex = (int)(*it).index;

				if (prevIt != mEdges.end() && (*prevIt).index == (uint32_t)curEdgeIndex)
				{
					int prevEdgeIndex = (*prevIt).index;

					// Delete both edges, without invalidating the iterator.
					auto it2 = it++;

					mEdges.erase(prevIt);
					mEdges.erase(it2);

					// Update edge ref
					if (mUpdateEdgesFunction)
					{
						mUpdateEdgesFunction(mwMesh, mMeshIndex, prevEdgeIndex, -1, true);
					}

					prevIt = mEdges.end();
				}
				else
				{
					prevIt = it++;
				}
			}

			invalidateOrderedVertexCache();
		}

		void DirectedEdgeLoop::cut(uint32_t fromVertexIndex, uint32_t toVertexIndex, IndexVector const& vertexIndices, IndexVector* newEdgeIndices, DirectedEdgeVector* removedEdges)
		{
			DirectedEdgeVector removeEdges;

			// By default we remove edges, unless we hit the from vertex first.
			bool removeMode = true;
			for (auto const& edge : mEdges)
			{
				// Check that the vertices aren't on the same edge
				if (((edge.v0 == fromVertexIndex && edge.v1 == toVertexIndex) ||
					(edge.v0 == toVertexIndex && edge.v1 == fromVertexIndex)) && vertexIndices.empty())
				{
					string errMsg = std::format("vertex {} and vertex {} share an edge in polygon {}.",
						fromVertexIndex, toVertexIndex, mMeshIndex);
					throw GeometryOperationException(__FUNCTION__, errMsg, true);
				}

				if (edge.v0 == fromVertexIndex)
				{
					if (removeMode)
					{
						// Clear remove list
						removeEdges.clear();
					}
					else
					{
						removeMode = true;
					}
				}
				if (edge.v0 == toVertexIndex)
				{
					removeMode = false;
				}

				if (removeMode)
				{
					removeEdges.push_back(edge);
				}
			}

			invalidateOrderedVertexCache();
			mwMesh->setIntegrityCheck(false);

			// Now actually remove the edges.  We may want leave them orphaned so
			// we can use them to create new polygon
			if (!removeEdges.empty())
			{
				auto edgeIt = mEdges.begin();
				while (edgeIt != mEdges.end())
				{
					uint32_t edgeIndex = (*edgeIt).index;
					if (find_if(removeEdges.begin(), removeEdges.end(),
						[edgeIndex](DirectedEdge edge)
					{
						return edgeIndex == edge.index;
					}) != removeEdges.end())
					{
						auto edgeIt2 = edgeIt++;
						mEdges.erase(edgeIt2);

						// Update edge refs
						if (mUpdateEdgesFunction)
						{
							mUpdateEdgesFunction(mwMesh, mMeshIndex, edgeIndex, -1, false);
						}
					}
					else
					{
						++edgeIt;
					}
				}
			}

			// Create new edges
			if (newEdgeIndices)
			{
				newEdgeIndices->clear();
			}

			uint32_t cutEdgeIndex, curVertexIndex = fromVertexIndex;
			for (uint32_t nextVertexIndex: vertexIndices)
			{
				cutEdgeIndex = mwMesh->addEdge(Edge(curVertexIndex, nextVertexIndex));
				addEdge(curVertexIndex, nextVertexIndex, cutEdgeIndex);

				curVertexIndex = nextVertexIndex;

				if (newEdgeIndices)
				{
					newEdgeIndices->push_back(cutEdgeIndex);
				}
			}

			cutEdgeIndex = mwMesh->addEdge(Edge(curVertexIndex, toVertexIndex));
			addEdge(curVertexIndex, toVertexIndex, cutEdgeIndex);

			mwMesh->popIntegrityCheck();

			if (newEdgeIndices)
			{
				newEdgeIndices->push_back(cutEdgeIndex);
			}
			if (removedEdges)
			{
				*removedEdges = removeEdges;
			}

			invalidateEdgeData();
		}

		DirectedEdgeIterator DirectedEdgeLoop::getFirstEdge() const
		{
			return mEdges.begin();
		}

		DirectedEdgeIterator DirectedEdgeLoop::getEndEdge() const
		{
			return mEdges.end();
		}

		list<DirectedEdgeIterator> DirectedEdgeLoop::getEdgesByFirstVertex(uint32_t vertexIndex) const
		{
			list<DirectedEdgeIterator> edges;

			for (auto it = mEdges.begin(); it != mEdges.end(); ++it)
			{
				if ((*it).v0 == vertexIndex)
				{
					edges.push_back(it);
				}
			}

			return edges;
		}

		list<DirectedEdgeIterator> DirectedEdgeLoop::getEdgesBySecondVertex(uint32_t vertexIndex) const
		{
			list<DirectedEdgeIterator> edges;

			for (auto it = mEdges.begin(); it != mEdges.end(); ++it)
			{
				if ((*it).v1 == vertexIndex)
				{
					edges.push_back(it);
				}
			}

			return edges;
		}

		DirectedEdgeIterator DirectedEdgeLoop::getEdgeByIndex(uint32_t edgeIndex) const
		{
			for (auto it = mEdges.begin(); it != mEdges.end(); ++it)
			{
				if ((*it).index == edgeIndex)
				{
					return it;
				}
			}

			return getEndEdge();
		}

		DirectedEdgeIterator DirectedEdgeLoop::getEdgeBySharedVertex(uint32_t vertexIndex, uint32_t edgeIndex) const
		{
			for (auto it = mEdges.begin(); it != mEdges.end(); ++it)
			{
				if ((*it).index == edgeIndex)
				{
					if ((*it).v0 == vertexIndex)
					{
						// Return previous edge
						if (it == mEdges.begin())
						{
							return --mEdges.end();
						}
						else
						{
							return --it;
						}
					}
					else if ((*it).v1 == vertexIndex)
					{
						// Return next edge
						++it;
						if (it == mEdges.end())
						{
							return mEdges.begin();
						}
						else
						{
							return it;
						}
					}
				}
			}

			return getEndEdge();
		}

		pair<uint32_t, uint32_t> DirectedEdgeLoop::getNeighbourEdges(uint32_t edgeIndex) const
		{
			pair<uint32_t, uint32_t> edgeIndices = make_pair((uint32_t)-1, (uint32_t)-1);

			for (auto it = mEdges.begin(); it != mEdges.end(); ++it)
			{
				auto const& edge = *it;
				if (edge.index == edgeIndex)
				{
					// Previous edge
					auto prevIt = it;
					if (prevIt == mEdges.begin())
					{
						prevIt = mEdges.end();
					}
					prevIt--;

					// Next edge
					auto nextIt = it;
					nextIt++;
					
					if (nextIt == mEdges.end())
					{
						nextIt = mEdges.begin();
					}

					edgeIndices.first = (*prevIt).index;
					edgeIndices.second = (*nextIt).index;
					break;
				}
			}

			return edgeIndices;
		}

		void DirectedEdgeLoop::reorderEdges(Mesh const* mesh)
		{
			list<MeshUtils::EdgeIndexInfo> edges;

			for (auto const& edge : mEdges)
			{
				edges.push_back(make_tuple((uint32_t)edge.index, (uint32_t)edge.v0, (uint32_t)edge.v1));
			}

			vector<vector<MeshUtils::EdgeIndexInfo>> edgeGroups = MeshUtils::groupConnectedEdges(edges);

			if (edgeGroups.size() != 1)
			{
				string errMsg = std::format("polygon {} has breaks in its edges.", mMeshIndex);
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			}

			mEdges.clear();
			vector<Vector2> vertexPositions;
			for (auto const& edgeInfo : edgeGroups[0])
			{
				DirectedEdge de;
				de.index = get<0>(edgeInfo);
				de.v0 = get<1>(edgeInfo);
				de.v1 = get<2>(edgeInfo);

				mEdges.push_back(de);
				vertexPositions.push_back(mesh->getVertex(de.v0).getPosition());
			}

			// Check winding, and reverse edges if they're the wrong way round.
			if (MathsUtils::pointsWinding(vertexPositions) != mWinding)
			{
				reverse(mEdges.begin(), mEdges.end());
				for (auto& edge: mEdges)
				{
					swap(edge.v0, edge.v1);
				}
			}

			invalidateOrderedVertexCache();
		}

		IndexSet DirectedEdgeLoop::getEdgeIndexSet() const
		{
			IndexSet indices;

			for (auto edge: mEdges)
			{
				indices.insert(edge.index);
			}

			return indices;
		}

		IndexVector DirectedEdgeLoop::getEdgeIndexList() const
		{
			IndexVector indices;

			for (auto edge : mEdges)
			{
				indices.push_back(edge.index);
			}

			return indices;
		}

		IndexSet DirectedEdgeLoop::getVertexIndexSet() const
		{
			IndexSet indices;

			for (auto edge: mEdges)
			{
				indices.insert(edge.v0);
				indices.insert(edge.v1);
			}

			return indices;
		}

		IndexVector DirectedEdgeLoop::getVertexIndexList() const
		{
			IndexVector indices;

			for (auto edge: mEdges)
			{
				indices.push_back(edge.v0);
			}

			return indices;
		}

		bool DirectedEdgeLoop::usesVertex(uint32_t vertexIndex) const
		{
			for (auto edge: mEdges)
			{
				if (vertexIndex == edge.v0 || vertexIndex == edge.v1)
				{
					return true;
				}
			}

			return false;
		}

		DirectedEdgeVector DirectedEdgeLoop::getOrderedEdges(IndexVector const& edgeIndices) const
		{
			DirectedEdgeVector orderedEdges;
			list<MeshUtils::EdgeIndexInfo> edges;

			for (uint32_t edgeIndex : edgeIndices)
			{
				auto edgeIt = find_if(mEdges.begin(), mEdges.end(), [edgeIndex](auto const& edge)
				{
					return edge.index == edgeIndex;
				});

				if (edgeIt != mEdges.end())
				{
					edges.push_back(make_tuple((uint32_t)(*edgeIt).index, (uint32_t)(*edgeIt).v0, (uint32_t)(*edgeIt).v1));
				}
				else
				{
					string errMsg = std::format("edge {} is used in polygon {}.", edgeIndex, mMeshIndex);
					throw GeometryOperationException(__FUNCTION__, errMsg, true);
				}
			}

			// Domino sort them.
			vector<vector<MeshUtils::EdgeIndexInfo>> edgeGroups = MeshUtils::groupConnectedEdges(edges);

			// Check for breaks: there should be at most one group
			if (edgeGroups.size() > 1)
			{
				string errMsg = std::format("there should be at most one group, instead there are {}.", edgeGroups.size());
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			}

			if (!edgeGroups.empty())
			{
				auto const& edgeInfoList = edgeGroups[0];

				for (auto const& edgeInfo : edgeInfoList)
				{
					DirectedEdge de;
					de.index = get<0>(edgeInfo);
					de.v0 = get<1>(edgeInfo);
					de.v1 = get<2>(edgeInfo);

					orderedEdges.push_back(de);
				}
			}

			return orderedEdges;
		}

		DirectedEdgeVector DirectedEdgeLoop::getEdges() const
		{
			return toDirectedEdgeVector(mEdges);
		}

		Vector2 DirectedEdgeLoop::getEdgeDirection(DirectedEdgeIterator edgeIt) const
		{
			auto const& polyEdge = *edgeIt;
			auto const& vertex0 = mwMesh->getVertex(polyEdge.v0);
			auto const& vertex1 = mwMesh->getVertex(polyEdge.v1);

			return vertex0.getPosition().directionTo(vertex1.getPosition());
		}

		Vector2 DirectedEdgeLoop::getEdgeNormal(DirectedEdgeIterator edgeIt) const
		{
			Vector2 dir = getEdgeDirection(edgeIt);
			return getEdgeDirection(edgeIt).perpendicular();
		}

		Vector2 DirectedEdgeLoop::getEdgeCentre(DirectedEdgeIterator edgeIt) const
		{
			auto const& polyEdge = *edgeIt;
			auto const& vertex0 = mwMesh->getVertex(polyEdge.v0);
			auto const& vertex1 = mwMesh->getVertex(polyEdge.v1);

			return vertex0.getPosition().lerp(vertex1.getPosition(), 0.5f);
		}

		BoundingBox DirectedEdgeLoop::getBoundingBox() const
		{
			auto vertexIndices = getOrderedVertexIndices(nullptr);

			float x0 = numeric_limits<float>::max();
			float y0 = numeric_limits<float>::max();
			float x1 = numeric_limits<float>::lowest();
			float y1 = numeric_limits<float>::lowest();

			for (uint32_t vertexIndex : vertexIndices)
			{
				auto bb = mwMesh->getVertex(vertexIndex).getBoundingBox();

				Vector2 minExtents, maxExtents;
				bb.getExtents(minExtents, maxExtents);

				if (minExtents.x < x0)
				{
					x0 = minExtents.x;
				}

				if (minExtents.y < y0)
				{
					y0 = minExtents.y;
				}

				if (maxExtents.x > x1)
				{
					x1 = maxExtents.x;
				}

				if (maxExtents.y > y1)
				{
					y1 = maxExtents.y;
				}
			}

			return BoundingBox(x0, y0, x1 - x0, y1 - y0);
		}

		IndexVector DirectedEdgeLoop::getConvexHullIndices() const
		{
			IndexVector result;

			// Start with lowermost (minimum y) vertex
			auto orderedIndices = getOrderedVertexIndices();
			
			uint32_t lowestIndex = 0, count = (uint32_t)orderedIndices.size();
			for (uint32_t i = 1; i < count; ++i)
			{
				auto const& vertexPos0 = mwMesh->getVertex(orderedIndices[i]).getPosition();
				auto const& vertexPos1 = mwMesh->getVertex(orderedIndices[lowestIndex]).getPosition();
				if (vertexPos0.x < vertexPos1.x)
				{
					lowestIndex = i;
				}
			}

			uint32_t p = lowestIndex, q;
			do
			{
				q = (p + 1) % count;

				Vector2 pp = mwMesh->getVertex(orderedIndices[p]).getPosition();
				for (uint32_t i = 0; i < count; ++i)
				{
					Vector2 pi = mwMesh->getVertex(orderedIndices[i]).getPosition();
					Vector2 pq = mwMesh->getVertex(orderedIndices[q]).getPosition();
					if (mWinding == Winding::Anticlockwise ?
						MathsUtils::sign(pp, pi, pq) > 0.0f :
						MathsUtils::sign(pp, pi, pq) < 0.0f)
					{
						q = i;
					}
				}

				result.push_back(orderedIndices[q]);
				p = q;
			} while (p != lowestIndex);
			
			return result;
		}

		bool DirectedEdgeLoop::pointInsideConvexHull(Vector2 const& point) const
		{
			return pointInsideConvexHull(point.x, point.y);
		}

		bool DirectedEdgeLoop::pointInsideConvexHull(float x, float y) const
		{
			IndexVector hullIndices = getConvexHullIndices();
			Vector2 point(x, y);

			// Check sign of distance is the same
			bool prevSign = false;
			for (uint32_t i = 0; i < hullIndices.size(); ++i)
			{
				uint32_t j = (i + 1) % hullIndices.size();
				Vector2 v0 = mwMesh->getVertex(hullIndices[i]).getPosition();
				Vector2 v1 = mwMesh->getVertex(hullIndices[j]).getPosition();

				bool thisSign = MathsUtils::sign(point, v0, v1) < 0.0f;
				if (i > 0 && thisSign != prevSign)
				{
					return false;
				}

				prevSign = thisSign;
			}

			return true;
		}

		int32_t DirectedEdgeLoop::getVertexAttributeIndex(uint32_t vertexIndex) const
		{
			auto dataIt = mVertexAttributeIndices.find(vertexIndex);
			if (dataIt != mVertexAttributeIndices.end())
			{
				return dataIt->second;
			}
			else
			{
				return -1;
			}
		}

		void DirectedEdgeLoop::setVertexAttributeIndex(uint32_t vertexIndex, int32_t attributeIndex)
		{
			mVertexAttributeIndices[vertexIndex] = attributeIndex;
		}

	} // geometry
} //WP_NAMESPACE
