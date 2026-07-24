#pragma once

#include <list>
#include <set>
#include <map>
#include <functional>

#include "willpower/common/Globals.h"
#include "willpower/common/BoundingBox.h"

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/DirectedEdge.h"
#include "willpower/geometry/Types.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class Mesh;

		class WP_GEOMETRY_API DirectedEdgeLoop
		{
			friend class Mesh;

			friend class MeshOperations;

			typedef std::function<void(Mesh*, uint32_t, int, int, bool)> UpdateRefFunction;

			typedef std::function<void(Mesh*, uint32_t)> DeleteFunction;

		private:

			DirectedEdgeList mEdges;

			UpdateRefFunction mUpdateEdgesFunction;

			// Ordered vertex index cache
			mutable bool mOrderedVertexIndicesCached;

			mutable IndexVector mOrderedVertexIndices;

			mutable std::pair<int, int> mOrderedVertexIndicesBreakIndices;

			mutable Winding mOrderedVertexIndicesWinding;

			std::map<uint32_t, int32_t> mVertexAttributeIndices;

		protected:

			Mesh* mwMesh;

			int32_t mMeshIndex;

			Winding mWinding;

			DeleteFunction mDeleteFunction;

		private:

			void copyFrom(DirectedEdgeLoop const& other);

			void updateEdgeFirstVertex(DirectedEdgeIterator edge, uint32_t vertexIndex);

			void updateEdgeSecondVertex(DirectedEdgeIterator edge, uint32_t vertexIndex);

			virtual void invalidateEdgeData() {}

		protected:

			void setMesh(Mesh* mesh, int32_t index);

			void setUpdateRefFunction(UpdateRefFunction func);

			void setDeleteFunction(DeleteFunction func);

			void invalidateOrderedVertexCache();

			IndexVector const& getCachedOrderedVertexIndices(std::pair<int, int>* breakIndices) const;

			void updateEdge(DirectedEdgeIterator edge, uint32_t firstVertex, uint32_t secondVertex, uint32_t edgeIndex);

			void removeEdge(uint32_t edgeIndex, bool ignoreIfNotPresent);

			void replaceEdge(uint32_t edgeIndex, uint32_t newEdgeIndex, uint32_t vertex0Index, uint32_t vertex1Index, bool fixAdjacent, bool ignoreIfNotPresent);

			void removeEdgesNotInSet(IndexSet const& edgeIndices);

			void removeTwoSidedEdges();

			virtual void cut(uint32_t fromVertexIndex, uint32_t toVertexIndex, IndexVector const& vertexIndices, IndexVector* newEdgeIndices = nullptr, DirectedEdgeVector* removedEdges = nullptr);

			void reorderEdges(Mesh const* mesh);

			void setWinding(Winding winding);

			DirectedEdgeIterator addEdge(uint32_t firstVertex, uint32_t secondVertex, uint32_t edgeIndex);

			DirectedEdgeIterator insertEdge(uint32_t firstVertex, uint32_t secondVertex, uint32_t edgeIndex, DirectedEdgeIterator where);

			DirectedEdgeIterator insertEdge(uint32_t firstVertex, uint32_t secondVertex, uint32_t edgeIndex);

		public:

			DirectedEdgeLoop(Winding winding, IndexVector const& edgeData);

			DirectedEdgeLoop(DirectedEdgeLoop const& other);

			DirectedEdgeLoop& operator=(DirectedEdgeLoop const& other);

			Winding getWinding() const;

			int getNumEdges() const;

			Mesh* getMesh();

			int32_t getMeshIndex() const;

			DirectedEdgeIterator getFirstEdge() const;

			DirectedEdgeIterator getEndEdge() const;

			std::list<DirectedEdgeIterator> getEdgesByFirstVertex(uint32_t vertexIndex) const;

			std::list<DirectedEdgeIterator> getEdgesBySecondVertex(uint32_t vertexIndex) const;

			DirectedEdgeIterator getEdgeByIndex(uint32_t edgeIndex) const;

			DirectedEdgeIterator getEdgeBySharedVertex(uint32_t vertexIndex, uint32_t edgeIndex) const;

			std::pair<uint32_t, uint32_t> getNeighbourEdges(uint32_t edgeIndex) const;

			IndexSet getEdgeIndexSet() const;

			IndexVector getEdgeIndexList() const;
			
			IndexVector getOrderedVertexIndices(std::pair<int, int>* breakIndices = nullptr) const;

			DirectedEdgeVector getEdges() const;

			IndexSet getVertexIndexSet() const;

			IndexVector getVertexIndexList() const;

			bool usesVertex(uint32_t vertexIndex) const;

			DirectedEdgeVector getOrderedEdges(IndexVector const& edgeIndices) const;

			Vector2 getEdgeDirection(DirectedEdgeIterator edgeIt) const;

			Vector2 getEdgeNormal(DirectedEdgeIterator edgeIt) const;

			Vector2 getEdgeCentre(DirectedEdgeIterator edgeIt) const;

			BoundingBox getBoundingBox() const;

			IndexVector getConvexHullIndices() const;

			bool pointInsideConvexHull(Vector2 const& point) const;

			bool pointInsideConvexHull(float x, float y) const;

			void setVertexAttributeIndex(uint32_t vertexIndex, int32_t attributeIndex);

			int32_t getVertexAttributeIndex(uint32_t vertexIndex) const;
		};

	} // geometry
} // WP_NAMESPACE
#pragma once
