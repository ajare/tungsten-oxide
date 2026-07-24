#include <cassert>
#include <iterator>
#include <algorithm>

#include <willpower/common/StringUtils.h>

#include "willpower/common/Globals.h"
#include "willpower/common/WillpowerWalker.h"
#include "willpower/common/BezierSpline.h"

#include "willpower/geometry/Mesh.h"
#include "willpower/geometry/MeshUtils.h"
#include "willpower/geometry/MeshOperations.h"
#include "willpower/geometry/Exception.h"
#include "willpower/geometry/TypeConverters.h"
#include "willpower/geometry/ConvexOffsetter.h"

#undef max

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace std;
		using namespace utils;

		Mesh::Mesh()
			: Mesh(nullptr, nullptr, nullptr, nullptr, nullptr)
		{
		}

		Mesh::Mesh(wp::Logger* logger)
			: Mesh(logger, nullptr, nullptr, nullptr, nullptr)
		{
		}

		Mesh::Mesh(
			UserAttributesFactory* vertexFactory, 
			UserAttributesFactory* edgeFactory, 
			UserAttributesFactory* polygonFactory,
			UserAttributesFactory* polygonVertexFactory)
			: Mesh(nullptr, vertexFactory, edgeFactory, polygonFactory, polygonVertexFactory)
		{
		}

		Mesh::Mesh(
			wp::Logger* logger, 
			UserAttributesFactory* vertexFactory, 
			UserAttributesFactory* edgeFactory, 
			UserAttributesFactory* polygonFactory,
			UserAttributesFactory* polygonVertexFactory)
			: mLogger(logger)
			, mVertexAttributeFactory(vertexFactory)
			, mEdgeAttributeFactory(edgeFactory)
			, mPolygonAttributeFactory(polygonFactory)
			, mPolygonVertexAttributeFactory(polygonVertexFactory)
			, mVertexAttributes(nullptr)
			, mEdgeAttributes(nullptr)
			, mPolygonAttributes(nullptr)
			, mPolygonVertexAttributes(nullptr)
			, mVertexAccelerationGrid(nullptr)
			, mEdgeAccelerationGrid(nullptr)
			, mPolygonAccelerationGrid(nullptr)
			, mMinExtent(numeric_limits<float>::max(), numeric_limits<float>::max())
			, mMaxExtent(numeric_limits<float>::lowest(), numeric_limits<float>::lowest())
			, mRecalculateExtents(false)
			, mVertexIdGenerator(0)
			, mEdgeIdGenerator(0)
			, mPolygonIdGenerator(0)
		{
			if (mVertexAttributeFactory)
			{
				mVertexAttributes = mVertexAttributeFactory->create();
			}
			if (mEdgeAttributeFactory)
			{
				mEdgeAttributes = mEdgeAttributeFactory->create();
			}
			if (mPolygonAttributeFactory)
			{
				mPolygonAttributes = mPolygonAttributeFactory->create();
			}
			if (mPolygonVertexAttributeFactory)
			{
				mPolygonVertexAttributes = mPolygonVertexAttributeFactory->create();
			}
			mCheckIntegrity.push(true);
		}

		Mesh::Mesh(Mesh const& other)
		{
			copyFrom(other);
		}

		Mesh::~Mesh()
		{
			delete mVertexAttributes;
			delete mEdgeAttributes;
			delete mPolygonAttributes;
			delete mPolygonVertexAttributes;

			delete mVertexAccelerationGrid;
			delete mEdgeAccelerationGrid;
			delete mPolygonAccelerationGrid;
		}

		Mesh& Mesh::operator=(Mesh const& other)
		{
			copyFrom(other);
			return *this;
		}

		void Mesh::copyFrom(Mesh const& other)
		{
			mRenderCallback = other.mRenderCallback;

			// Copy all data
			mVertices = other.mVertices;
			mDeadVertices = other.mDeadVertices;
			mEdges = other.mEdges;
			mDeadEdges = other.mDeadEdges;
			mPolygons = other.mPolygons;
			mDeadPolygons = other.mDeadPolygons;
			mVerticesToEdges = other.mVerticesToEdges;
			mCallbacks = other.mCallbacks;
			mMinExtent = other.mMinExtent;
			mMaxExtent = other.mMaxExtent;
			mRecalculateExtents = other.mRecalculateExtents;
			mVertexIdGenerator = other.mVertexIdGenerator;
			mEdgeIdGenerator = other.mEdgeIdGenerator;
			mPolygonIdGenerator = other.mPolygonIdGenerator;

			// Copy user vertex attributes
			mVertexAttributeFactory = other.mVertexAttributeFactory;
			if (other.mVertexAttributes)
			{
				mVertexAttributes = mVertexAttributeFactory->copy(other.mVertexAttributes);
			}
			else
			{
				mVertexAttributes = nullptr;
			}

			// Copy user edge attributes
			mEdgeAttributeFactory = other.mEdgeAttributeFactory;
			if (other.mEdgeAttributes)
			{
				mEdgeAttributes = mEdgeAttributeFactory->copy(other.mEdgeAttributes);
			}
			else
			{
				mEdgeAttributes = nullptr;
			}

			// Copy user polygon attributes
			mPolygonAttributeFactory = other.mPolygonAttributeFactory;
			if (other.mPolygonAttributes)
			{
				mPolygonAttributes = mPolygonAttributeFactory->copy(other.mPolygonAttributes);
			}
			else
			{
				mPolygonAttributes = nullptr;
			}

			// Copy user polygon vertex attributes
			mPolygonVertexAttributeFactory = other.mPolygonVertexAttributeFactory;
			if (other.mPolygonVertexAttributes)
			{
				mPolygonVertexAttributes = mPolygonVertexAttributeFactory->copy(other.mPolygonVertexAttributes);
			}
			else
			{
				mPolygonVertexAttributes = nullptr;
			}

			// Copy acceleration grids
			if (other.mVertexAccelerationGrid)
			{
				mVertexAccelerationGrid = new AccelerationGrid(*other.mVertexAccelerationGrid);
			}
			else
			{
				mVertexAccelerationGrid = nullptr;
			}

			if (other.mEdgeAccelerationGrid)
			{
				mEdgeAccelerationGrid = new AccelerationGrid(*other.mEdgeAccelerationGrid);
			}
			else
			{
				mEdgeAccelerationGrid = nullptr;
			}

			if (other.mPolygonAccelerationGrid)
			{
				mPolygonAccelerationGrid = new AccelerationGrid(*other.mPolygonAccelerationGrid);
			}
			else
			{
				mPolygonAccelerationGrid = nullptr;
			}

			// Fix up callbacks
			for (uint32_t i = 0; i < mVertices.size(); ++i)
			{
				mVertices[i].setMesh(this, i);
				mVertices[i].setDeleteFunction(&Mesh::_killVertex);
				mVertices[i].setUpdateEdgeFunction(&Mesh::_updateVertexEdgeReference);
			}

			for (uint32_t i = 0; i < mEdges.size(); ++i)
			{
				mEdges[i].setMesh(this, i);
				mEdges[i].setDeleteFunction(&Mesh::_killEdge);
				mEdges[i].setUpdateRefFunction(&Mesh::_updateVertexEdgeReferences);
			}

			for (uint32_t i = 0; i < mPolygons.size(); ++i)
			{
				mPolygons[i].setMesh(this, i);
				mPolygons[i].setDeleteFunction(&Mesh::_killPolygon);
				mPolygons[i].setUpdateRefFunction(&Mesh::_updateEdgePolygonReferences);
			}
		}

		void Mesh::recalculateExtents() const
		{
			if (!mRecalculateExtents)
			{
				return;
			}

			mMinExtent.set(numeric_limits<float>::max(), numeric_limits<float>::max());
			mMaxExtent.set(numeric_limits<float>::lowest(), numeric_limits<float>::lowest());

			for (auto const& vertex: mVertices)
			{
				auto const& vertexPosition = vertex.getPosition();

				if (vertexPosition.x < mMinExtent.x)
				{
					mMinExtent.x = vertexPosition.x;
				}
				if (vertexPosition.y < mMinExtent.y)
				{
					mMinExtent.y = vertexPosition.y;
				}
				if (vertexPosition.x > mMaxExtent.x)
				{
					mMaxExtent.x = vertexPosition.x;
				}
				if (vertexPosition.y > mMaxExtent.y)
				{
					mMaxExtent.y = vertexPosition.y;
				}
			}

			mRecalculateExtents = false;
		}

		void Mesh::setRenderCallback(RenderCallback callback)
		{
			mRenderCallback = callback;
		}

		void Mesh::renderCallback(void* data)
		{
			if (mRenderCallback)
			{
				mRenderCallback(this, data);
			}
		}

		void Mesh::clear()
		{
			mVertices.clear();
			mDeadVertices.clear();

			mEdges.clear();
			mDeadEdges.clear();

			mPolygons.clear();
			mDeadPolygons.clear();

			mVerticesToEdges.clear();

			mCheckIntegrity.push(true);

			mRecalculateExtents = true;
			recalculateExtents();

			if (mVertexAttributes)
			{
				mVertexAttributes->clear();
			}

			if (mEdgeAttributes)
			{
				mEdgeAttributes->clear();
			}

			if (mPolygonAttributes)
			{
				mPolygonAttributes->clear();
			}

			if (mPolygonVertexAttributes)
			{
				mPolygonVertexAttributes->clear();
			}

			if (mVertexAccelerationGrid)
			{
				mVertexAccelerationGrid->clear();
			}
			if (mEdgeAccelerationGrid)
			{
				mEdgeAccelerationGrid->clear();
			}
			if (mPolygonAccelerationGrid)
			{
				mPolygonAccelerationGrid->clear();
			}

			mVertexIdGenerator = 0;
			mEdgeIdGenerator = 0;
			mPolygonIdGenerator = 0;
		}

		uint32_t Mesh::addVertex(Vertex vertex, int32_t prototype)
		{
			uint32_t index = (uint32_t)mVertices.size();

			mVertices.push_back(vertex);

			auto& addedVertex = mVertices.back();

			addedVertex.setMesh(this, index);
			addedVertex.setDeleteFunction(&Mesh::_killVertex);
			addedVertex.setUpdateEdgeFunction(&Mesh::_updateVertexEdgeReference);
			addedVertex.mPublicId = mVertexIdGenerator++;
	
		#ifdef CHECK_INTEGRITY
			checkIntegrity();
		#endif

			// Acceleration grid
			if (mVertexAccelerationGrid)
			{
				mVertexAccelerationGrid->addItem(index, addedVertex.getBoundingBox());
			}

			// Update extents
			mRecalculateExtents = true;

			// Callbacks
			for (auto const& callbacks: mCallbacks)
			{
				auto const& callback = callbacks.second;
				if (callback.onAddVertex)
				{
					callback.onAddVertex(index, prototype);
				}
			}

			return index;
		}
		
		void Mesh::removeVertex(uint32_t vertexIndex, RemoveVertexResult* result)
		{
			auto const& vertex = getVertex(vertexIndex);

			// Get edges that this vertex uses.  Take a copy so as to not invalidate iterators.
			IndexSet edgeRefIndices = vertex.getEdgeReferences();

			// Get polygons that the edges use
			IndexSet polygonsUsedIndices;

			for (auto edgeRefIndex: edgeRefIndices)
			{
				Edge const& edge = getEdge(edgeRefIndex);
				IndexSet polygonRefIndices = edge.getPolygonReferences();

				// Add to polygons used
				set_union(
					polygonsUsedIndices.begin(), polygonsUsedIndices.end(),
					polygonRefIndices.begin(), polygonRefIndices.end(),
					inserter(polygonsUsedIndices, polygonsUsedIndices.end()));

				// Update the polygons that use the edge, which will in turn remove the edge.
				for (auto polygonRefIndex: polygonRefIndices)
				{
					Polygon& polygonRef = _getPolygon(polygonRefIndex);
					polygonRef.removeEdge(edgeRefIndex, false);
				}
			}

			// If the vertex only has one polygon, then close that polygon,
			// otherwise merge the polygons.
			uint32_t newEdgeIndex = (uint32_t)-1;
			switch (polygonsUsedIndices.size())
			{
			case 0:
#pragma warning(suppress: 4127)
				ASSERT_TRACE(false && "Mesh::removeVertex(): vertex with no polygons: an orphan?");
				break;

			case 1:
				_closePolygon(*polygonsUsedIndices.begin(), true, *edgeRefIndices.begin(), &newEdgeIndex);
				break;

			default:
				_closePolygon(_mergePolygons(polygonsUsedIndices), true, *edgeRefIndices.begin(), &newEdgeIndex);
				break;
			}

			if (result)
			{
				result->newEdgeIndex = newEdgeIndex;
			}

#ifdef CHECK_INTEGRITY
			checkIntegrity();
#endif
		}

		void Mesh::removeVertices(IndexVector const& vertexIndices)
		{
			bool first = true;
			for (auto vertexIndex: vertexIndices)
			{
				try
				{
					removeVertex(vertexIndex);
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
			}
		}

		void Mesh::killVertex(uint32_t vertexIndex)
		{
			ASSERT_TRACE(mDeadVertices.find(vertexIndex) == mDeadVertices.end() && "Mesh::killVertex(): vertex is already marked as dead.");
			mDeadVertices.insert(vertexIndex);

			// Acceleration grid
			if (mVertexAccelerationGrid)
			{
				mVertexAccelerationGrid->removeItem(vertexIndex);
			}

			// Callbacks
			for (auto const& callbacks: mCallbacks)
			{
				auto const& callback = callbacks.second;
				if (callback.onRemoveVertex)
				{
					callback.onRemoveVertex(vertexIndex);
				}
			}
		}

		uint32_t Mesh::getNumVertices() const
		{
			return (uint32_t)(mVertices.size() - mDeadVertices.size());
		}

		uint32_t Mesh::getFirstVertexIndex() const
		{
			for (uint32_t i = 0; i < mVertices.size(); ++i)
			{
				if (isVertexAlive(i))
				{
					return i;
				}
			}

			return (uint32_t)mVertices.size();
		}

		uint32_t Mesh::getNextVertexIndex(uint32_t vertexIndex) const
		{
			for (uint32_t i = vertexIndex + 1; i < mVertices.size(); ++i)
			{
				if (isVertexAlive(i))
				{
					return i;
				}
			}

			return (uint32_t)mVertices.size();
		}

		bool Mesh::vertexIndexIterationFinished(uint32_t vertexIndex) const
		{
			return vertexIndex == mVertices.size();
		}

		void Mesh::moveVertex(uint32_t vertexIndex, Vector2 const& move)
		{
			auto& vertex = _getVertex(vertexIndex);

			vertex.translatePosition(move);

			// Acceleration grid
			if (mVertexAccelerationGrid)
			{
				mVertexAccelerationGrid->moveItem(vertexIndex, vertex.getBoundingBox());
			}

			// Update polygons
			auto const& edgeRefs = vertex.getEdgeReferences();
			for (uint32_t edgeRef: edgeRefs)
			{
				// Update edge acceleration grid, now that edge vertices have moved.
				if (mEdgeAccelerationGrid)
				{
					mEdgeAccelerationGrid->moveItem(edgeRef, getEdge(edgeRef).getBoundingBox());
				}

				auto const& polygonRefs = getEdge(edgeRef).getPolygonReferences();
				for (uint32_t polygonRef: polygonRefs)
				{
					auto& polyToUpdate = _getPolygon(polygonRef);
					polyToUpdate.invalidateTriangleData();

					// Update polygon acceleration grid
					if (mPolygonAccelerationGrid)
					{
						mPolygonAccelerationGrid->moveItem(polygonRef, polyToUpdate.getBoundingBox());
					}
				}
			}

			// Update extents
			mRecalculateExtents = true;
		}

		void Mesh::moveVertexTo(uint32_t vertexIndex, Vector2 const& pos)
		{
			Vector2 move = pos - getVertex(vertexIndex).getPosition();
			moveVertex(vertexIndex, move);
		}

		void Mesh::moveVertices(IndexVector const& vertexIndices, Vector2 const& move)
		{
			for (auto vertexIndex : vertexIndices)
			{
				moveVertex(vertexIndex, move);
			}
		}
		
		void Mesh::setVertexUserData(uint32_t vertexIndex, void const* data)
		{
			auto& vertex = _getVertex(vertexIndex);
			if (data)
			{
				auto dataId = vertex.mAttributeIndex;

				if (dataId < 0)
				{
					vertex.mAttributeIndex = (int32_t)mVertexAttributes->createAttribute(data);
				}
				else
				{
					mVertexAttributes->updateAttribute(dataId, data);
				}
			}
			else
			{
				vertex.mAttributeIndex = -1;
			}
		}

		void const* Mesh::getVertexUserData(uint32_t vertexIndex) const
		{
			auto const& vertex = getVertex(vertexIndex);
			auto dataId = vertex.mAttributeIndex;

			if (dataId < 0)
			{
				return nullptr;
			}
			else
			{
				return mVertexAttributes->readAttribute(dataId);
			}
		}

		void Mesh::getVertexUvAttribute(uint32_t vertexIndex, uint32_t textureIndex, float& u, float& v) const
		{
			auto userData = getVertexUserData(vertexIndex);
			if (userData)
			{
				mVertexAttributes->getUvAttribute(userData, textureIndex, u, v);
			}
			else
			{
				u = 0.0f;
				v = 0.0f;
			}
		}

		void Mesh::getVertexRgbaAttribute(uint32_t vertexIndex, float& r, float& g, float& b, float& a) const
		{
			auto userData = getVertexUserData(vertexIndex);
			if (userData)
			{
				mVertexAttributes->getRgbaAttribute(userData, r, g, b, a);
			}
			else
			{
				r = 1.0f;
				g = 1.0f;
				b = 1.0f;
				a = 1.0f;
			}
		}

		void Mesh::setEdgeUserData(uint32_t edgeIndex, void const* data)
		{
			auto& edge = _getEdge(edgeIndex);
			auto dataId = edge.mAttributeIndex;

			if (data)
			{
				if (dataId < 0)
				{
					edge.mAttributeIndex = (int32_t)mEdgeAttributes->createAttribute(data);
				}
				else
				{
					mEdgeAttributes->updateAttribute(dataId, data);
				}
			}
			else
			{
				edge.mAttributeIndex = -1;
			}
		}

		void const* Mesh::getEdgeUserData(uint32_t edgeIndex) const
		{
			auto const& edge = getEdge(edgeIndex);
			auto dataId = edge.mAttributeIndex;

			if (dataId < 0)
			{
				return nullptr;
			}
			else
			{
				return mEdgeAttributes->readAttribute(dataId);
			}
		}

		void Mesh::getEdgeRgbaAttribute(uint32_t edgeIndex, float& r, float& g, float& b, float& a) const
		{
			auto userData = getEdgeUserData(edgeIndex);
			if (userData)
			{
				mEdgeAttributes->getRgbaAttribute(userData, r, g, b, a);
			}
			else
			{
				r = 1.0f;
				g = 1.0f;
				b = 1.0f;
				a = 1.0f;
			}
		}

		void Mesh::setPolygonUserData(uint32_t polygonIndex, void const* data)
		{
			auto& polygon = _getPolygon(polygonIndex);
			auto dataId = polygon.mAttributeIndex;

			if (data)
			{
				if (dataId < 0)
				{
					polygon.mAttributeIndex = (int32_t)mPolygonAttributes->createAttribute(data);
				}
				else
				{
					mPolygonAttributes->updateAttribute(dataId, data);
				}
			}
			else
			{
				polygon.mAttributeIndex = -1;
			}
		}

		void const* Mesh::getPolygonUserData(uint32_t polygonIndex) const
		{
			auto& polygon = getPolygon(polygonIndex);
			auto dataId = polygon.mAttributeIndex;

			if (dataId < 0)
			{
				return nullptr;
			}
			else
			{
				return mPolygonAttributes->readAttribute(dataId);
			}
		}

		void Mesh::getPolygonRgbaAttribute(uint32_t polygonIndex, float& r, float& g, float& b, float& a) const
		{
			auto userData = getPolygonUserData(polygonIndex);
			if (userData)
			{
				mPolygonAttributes->getRgbaAttribute(userData, r, g, b, a);
			}
			else
			{
				r = 1.0f;
				g = 1.0f;
				b = 1.0f;
				a = 1.0f;
			}
		}

		void Mesh::getPolygonMaterialAttribute(uint32_t polygonIndex, string& material) const
		{
			auto userData = getPolygonUserData(polygonIndex);
			if (userData)
			{
				mPolygonAttributes->getMaterialAttribute(userData, material);
			}
			else
			{
				material = "";
			}
		}

		void Mesh::getPolygonProgramAttribute(uint32_t polygonIndex, string& program) const
		{
			auto userData = getPolygonUserData(polygonIndex);
			if (userData)
			{
				mPolygonAttributes->getProgramAttribute(userData, program);
			}
			else
			{
				program = "";
			}
		}

		void Mesh::getPolygonTexturesAttribute(uint32_t polygonIndex, vector<string>& textures) const
		{
			auto userData = getPolygonUserData(polygonIndex);
			if (userData)
			{
				mPolygonAttributes->getTexturesAttribute(userData, textures);
			}
			else
			{
				textures.clear();
			}
		}

		void Mesh::getPolygonColourType(uint32_t polygonIndex, UserAttributePolygonColourType& type) const
		{
			auto userData = getPolygonUserData(polygonIndex);
			if (userData)
			{
				mPolygonAttributes->getPolygonColourType(userData, type);
			}
			else
			{
				type = UserAttributePolygonColourType::Default;
			}
		}

		void Mesh::setPolygonVertexUserData(uint32_t polygonIndex, uint32_t vertexIndex, void const* data)
		{
			auto& polygon = _getPolygon(polygonIndex);
			auto attribIndex = polygon.getVertexAttributeIndex(vertexIndex);

			if (attribIndex >= 0)
			{
				mPolygonVertexAttributes->updateAttribute(attribIndex, data);
			}
			else
			{
				attribIndex = (int32_t)mPolygonVertexAttributes->createAttribute(data);
				polygon.setVertexAttributeIndex(vertexIndex, attribIndex);
			}
		}

		void const* Mesh::getPolygonVertexUserData(uint32_t polygonIndex, uint32_t vertexIndex) const
		{
			auto const& polygon = getPolygon(polygonIndex);
			auto attribIndex = polygon.getVertexAttributeIndex(vertexIndex);

			if (attribIndex >= 0)
			{
				return mPolygonVertexAttributes->readAttribute(attribIndex);
			}
			else
			{
				return nullptr;
			}
		}

		void Mesh::getPolygonVertexUvAttribute(uint32_t polygonIndex, uint32_t vertexIndex, uint32_t textureIndex, float& u, float& v) const
		{
			auto userData = getPolygonVertexUserData(polygonIndex, vertexIndex);

			if (userData)
			{
				mPolygonVertexAttributes->getUvAttribute(userData, textureIndex, u, v);
			}
			else
			{
				u = 0.0f;
				v = 0.0f;
			}
		}

		void Mesh::getPolygonVertexUvWeightAttribute(uint32_t polygonIndex, uint32_t vertexIndex, uint32_t textureIndex, float& weight) const
		{
			auto userData = getPolygonVertexUserData(polygonIndex, vertexIndex);

			if (userData)
			{
				mPolygonVertexAttributes->getUvWeightAttribute(userData, textureIndex, weight);
			}
			else
			{
				weight = 1.0f;
			}
		}

		void Mesh::getPolygonVertexRgbaAttribute(uint32_t polygonIndex, uint32_t vertexIndex, float& r, float& g, float& b, float& a) const
		{
			auto userData = getPolygonVertexUserData(polygonIndex, vertexIndex);

			if (userData)
			{
				mPolygonVertexAttributes->getRgbaAttribute(userData, r, g, b, a);
			}
			else
			{
				r = 1.0f;
				g = 1.0f;
				b = 1.0f;
				a = 1.0f;
			}
		}

		Vertex const& Mesh::getVertex(uint32_t vertexIndex) const
		{
			ASSERT_TRACE(vertexIndex < mVertices.size() && "Mesh::getVertex(): index is out of range.");
			ASSERT_TRACE(mDeadVertices.find(vertexIndex) == mDeadVertices.end() && "Mesh::getVertex(): vertex is on dead list.");

			return mVertices[vertexIndex];
		}

		Vertex& Mesh::_getVertex(uint32_t vertexIndex)
		{
			ASSERT_TRACE(vertexIndex < mVertices.size() && "Mesh::_getVertex(): index is out of range.");
			ASSERT_TRACE(mDeadVertices.find(vertexIndex) == mDeadVertices.end() && "Mesh::getVertex(): vertex is on dead list.");

			return mVertices[vertexIndex];
		}

		uint32_t Mesh::addEdge(Edge edge, int32_t prototype)
		{
			uint32_t index = addEdgeImpl(edge, prototype);

		#ifdef CHECK_INTEGRITY
			checkIntegrity();
		#endif

			return index;
		}

		uint32_t Mesh::addEdgeImpl(Edge const& edge, int32_t prototype)
		{
			uint32_t index = (uint32_t)mEdges.size();

			int32_t firstVertex = edge.getFirstVertex();
			int32_t secondVertex = edge.getSecondVertex();

			// Update vertex references to this edge.
			mVertices[firstVertex].addEdgeReference(index);
			mVertices[secondVertex].addEdgeReference(index);

			mEdges.push_back(edge);
			auto& addedEdge = mEdges.back();

			addedEdge.setMesh(this, index);
			addedEdge.setDeleteFunction(&Mesh::_killEdge);
			addedEdge.setUpdateRefFunction(&Mesh::_updateVertexEdgeReferences);
			addedEdge.mPublicId = mEdgeIdGenerator++;

			// Add to reverse lookup
			addEdgeToReverseLookup(addedEdge, index);

			// Acceleration grid
			if (mEdgeAccelerationGrid)
			{
				mEdgeAccelerationGrid->addItem(index, addedEdge.getBoundingBox());
			}

			// Callbacks
			for (auto const& callbacks: mCallbacks)
			{
				auto const& callback = callbacks.second;
				if (callback.onAddEdge)
				{
					callback.onAddEdge(index, prototype);
				}
			}

			return index;
		}

		void Mesh::killEdge(uint32_t edgeIndex)
		{
			ASSERT_TRACE(mDeadEdges.find(edgeIndex) == mDeadEdges.end() && "Mesh::killEdge(): edge is already marked as dead.");

			auto const& edge = getEdge(edgeIndex);

			// Remove edge references from the vertices used.
			auto& firstVertex = _getVertex(edge.getFirstVertex());
			firstVertex.removeEdgeReference(edgeIndex);

			auto& secondVertex = _getVertex(edge.getSecondVertex());
			secondVertex.removeEdgeReference(edgeIndex);

			mDeadEdges.insert(edgeIndex);

			// Remove from reverse lookup
			removeEdgeFromReverseLookup(edge);

			// Acceleration grid
			if (mEdgeAccelerationGrid)
			{
				mEdgeAccelerationGrid->removeItem(edgeIndex);
			}

			// Callbacks
			for (auto const& callbacks: mCallbacks)
			{
				auto const& callback = callbacks.second;
				if (callback.onRemoveEdge)
				{
					callback.onRemoveEdge(edgeIndex);
				}
			}
		}

		void Mesh::moveEdge(uint32_t edgeIndex, Vector2 const& move)
		{
			auto& edge = _getEdge(edgeIndex);

			// This will also update edge and polygon cached data
			moveVertex(edge.getFirstVertex(), move);
			moveVertex(edge.getSecondVertex(), move);
		}

		void Mesh::moveEdges(IndexVector const& edgeIndices, Vector2 const& move)
		{
			// Get unique vertices so we don't move the same one more than once.
			IndexSet uniqueVertices;

			for (auto edgeIndex : edgeIndices)
			{
				auto const& edge = getEdge(edgeIndex);
				uniqueVertices.insert(edge.getFirstVertex());
				uniqueVertices.insert(edge.getSecondVertex());
			}

			// Move them.
			IndexVector vertexList;
			vertexList.assign(uniqueVertices.begin(), uniqueVertices.end());

			moveVertices(vertexList, move);
		}

		void Mesh::setEdgeLength(uint32_t edgeIndex, float length)
		{
			auto const& edge = getEdge(edgeIndex);

			auto edgeCentre = edge.getCentre();
			auto vertexOffset = edge.getDirection() * length / 2.0f;

			moveVertexTo(edge.getFirstVertex(), edgeCentre - vertexOffset);
			moveVertexTo(edge.getSecondVertex(), edgeCentre + vertexOffset);
		}

		vector<DirectedEdgeVector> Mesh::getExtrusionSections(uint32_t polygonIndex, IndexVector const& edgeIndices, bool separate, bool allowLoop)
		{
			auto const& polygon = getPolygon(polygonIndex);

			vector<DirectedEdgeVector> sections;
			if (separate)
			{
				for (uint32_t edgeIndex: edgeIndices)
				{
					DirectedEdge de = *polygon.getEdgeByIndex(edgeIndex);

					DirectedEdgeVector iv(1, de);
					sections.push_back(iv);
				}
			}
			else
			{
				auto edgeGroups = MeshUtils::groupConnectedEdges(this, polygonIndex, edgeIndices);
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

		void Mesh::extrudePolygonEdgesDirected(uint32_t polygonIndex, DirectedEdgeVector const& edgeData, Vector2 const& extrusion, ExtrudePolygonOptions options, uint32_t* endVertices, uint32_t* collinearVertices, uint32_t* newPolygonIndex, uint32_t* holeIndex, IndexVector* extrudedEdgeIndices, IndexVector* sourceEdgeIndices)
		{
			WP_UNUSED(holeIndex);

			// Get vertices
			vector<Vector2> vertexPositions;
			IndexVector vertexIndices;

			if (edgeData.front().v0 == edgeData.back().v1)
			{
				throw GeometryOperationException(__FUNCTION__, "cannot extrude a full loop.", true);
			}

			for (auto const& de: edgeData)
			{
				vertexPositions.push_back(getVertex(de.v0).getPosition());
				vertexIndices.push_back(de.v0);
			}

			auto const& edge = getEdge(edgeData.back().index);
			vertexPositions.push_back(getVertex(edge.getSecondVertex()).getPosition());
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
					MeshOperations::splitEdge(this, edgeData.front().index, 3, &ser);
					v0Index = ser.newVertexIndices[0];
					v1Index = ser.newVertexIndices[1];

					extrudedEdgeIndices->push_back(ser.newEdgeIndices[1]);
				}
				else
				{
					MeshOperations::splitEdge(this, edgeData.front().index, 2, &ser);
					v0Index = ser.newVertexIndices[0];

					extrudedEdgeIndices->push_back(ser.newEdgeIndices[1]);

					for (uint32_t i = 1; i < edgeData.size() - 1; ++i)
					{
						extrudedEdgeIndices->push_back(edgeData[i].index);
					}
					
					MeshOperations::splitEdge(this, edgeData.back().index, 2, &ser);
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
				moveVertexTo(v0Index, vertexPositions.front());
				moveVertexTo(v1Index, vertexPositions.back());

				vertexIndices.front() = v0Index;
				vertexIndices.back() = v1Index;

				// Move vertices
				for (uint32_t i = 0; i < vertexIndices.size(); ++i)
				{
					moveVertexTo(vertexIndices[i], vertexPositions[i] + extrusion);
				}

				*newPolygonIndex = polygonIndex;
			}
			else
			{
				// Create vertices
				for (Vector2 const& vertexPos: vertexPositions)
				{
					uint32_t vIndex = addVertex(Vertex(vertexPos + extrusion));
					newVertexIndices.push_back(vIndex);
				}

				// Store old end vertices for checking collinearity
				collinearVertices[0] = vertexIndices.front();
				collinearVertices[1] = vertexIndices.back();

				// Store new end vertices for tapering/chamfering
				endVertices[0] = newVertexIndices.front();
				endVertices[1] = newVertexIndices.back();

				// Source edges are those in edgeData
				for (auto const& de: edgeData)
				{
					sourceEdgeIndices->push_back(de.index);
				}

				// Create edges
				IndexVector newEdgeData;
				for (uint32_t i = 0; i < newVertexIndices.size() - 1; ++i)
				{
					uint32_t eIndex = addEdge(Edge(newVertexIndices[i], newVertexIndices[i + 1]));
					newEdgeData.push_back(newVertexIndices[i]);
					newEdgeData.push_back(newVertexIndices[i + 1]);
					newEdgeData.push_back(eIndex);

					extrudedEdgeIndices->push_back(eIndex);
				}

				// Create edges from new loop to old.
				Edge e0(edgeData.front().v0, newVertexIndices.front());
				Edge e1(Edge(newVertexIndices.back(), edgeData.back().v1));
				uint32_t e0Index = addEdge(e0);
				uint32_t e1Index = addEdge(e1);

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
				*newPolygonIndex = addPolygon(newPolygon);
			}
		}

		void Mesh::extrudePolygonEdgesNormal(uint32_t polygonIndex, DirectedEdgeVector const& edgeData, float distance, ExtrudePolygonOptions options, uint32_t* endVertices, uint32_t* collinearVertices, uint32_t* newPolygonIndex, uint32_t* holeIndex, IndexVector* extrudedEdgeIndices, IndexVector* sourceEdgeIndices)
		{
			// Get vertices
			vector<Vector2> vertexPositions;

			bool isLoop = edgeData.front().v0 == edgeData.back().v1;

			for (auto const& de: edgeData)
			{
				vertexPositions.push_back(getVertex(de.v0).getPosition());
			}

			if (edgeData.front().v0 != edgeData.back().v1)
			{
				auto const& edge = getEdge(edgeData.back().index);
				vertexPositions.push_back(getVertex(edge.getSecondVertex()).getPosition());
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
					for (auto const& pos: extrudedVertexPositions)
					{
						newVertexIndices.push_back(addVertex(Vertex(pos)));
					}

					IndexVector newEdgeData;
					for (uint32_t i = 0; i < newVertexIndices.size() - 1; ++i)
					{
						uint32_t edgeIndex = addEdge(Edge(newVertexIndices[i], newVertexIndices[i + 1]));
						extrudedEdgeIndices->push_back(edgeIndex);

						newEdgeData.push_back(newVertexIndices[i]);
						newEdgeData.push_back(newVertexIndices[i + 1]);
						newEdgeData.push_back(edgeIndex);
					}

					// Join up loop.
					uint32_t edgeIndex = addEdge(Edge(newVertexIndices.back(), newVertexIndices.front()));
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
					*newPolygonIndex = addPolygon(newPolygon);

					// Remove old polygon, transferring hole ownership
					auto holeIndices = getPolygon(polygonIndex).getHoleIndices();
					removePolygon(polygonIndex, false);

					for (uint32_t hIndex : holeIndices)
					{
						addHoleToPolygon(*newPolygonIndex, hIndex);
					}
				}
				else
				{
					// Split edges and move vertices into place.
					int numEdgesToCreate = (int)extrudedVertexPositions.size() - (int)(edgeData.size() - 2);
					ASSERT_TRACE(numEdgesToCreate > 0 && "Mesh::extrudePolygonEdgesNormal(): invalid offsetting.");

					SplitEdgeResult ser;
					MeshOperations::splitEdge(this, edgeData.front().index, numEdgesToCreate, &ser);

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
						moveVertexTo(vertexIndices[i], extrudedVertexPositions[i]);
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
					newVertexIndices.push_back(addVertex(Vertex(pos)));
				}

				IndexVector newEdgeData;
				for (uint32_t i = 0; i < newVertexIndices.size() - 1; ++i)
				{
					uint32_t edgeIndex = addEdge(Edge(newVertexIndices[i], newVertexIndices[i + 1]));
					extrudedEdgeIndices->push_back(edgeIndex);

					newEdgeData.push_back(newVertexIndices[i]);
					newEdgeData.push_back(newVertexIndices[i + 1]);
					newEdgeData.push_back(edgeIndex);
				}

				if (isLoop)
				{
					// Extrude new polygon and add old polygon as filled in hole.
					// Join up loop.
					uint32_t edgeIndex = addEdge(Edge(newVertexIndices.back(), newVertexIndices.front()));
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
					for (auto const& de: edgeData)
					{
						sourceEdgeIndices->push_back(de.index);
					}

					// Create polygon
					Polygon newPolygon(newEdgeData);
					*newPolygonIndex = addPolygon(newPolygon);

					// Add old as hole and fill in.
					addFilledHoleToPolygon(*newPolygonIndex, polygonIndex, holeIndex);
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
					for (auto const& de: edgeData)
					{
						sourceEdgeIndices->push_back(de.index);
					}

					// Create edges from new loop to old.
					Edge e0(edgeData.front().v0, newVertexIndices.front());
					Edge e1(Edge(newVertexIndices.back(), edgeData.back().v1));
					uint32_t e0Index = addEdge(e0);
					uint32_t e1Index = addEdge(e1);

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
					*newPolygonIndex = addPolygon(newPolygon);
				}
			}
		}

		uint32_t Mesh::getNumEdges() const
		{
			return (uint32_t)(mEdges.size() - mDeadEdges.size());
		}

		uint32_t Mesh::getFirstEdgeIndex() const
		{
			auto numEdges = (uint32_t)mEdges.size();
			for (uint32_t i = 0; i < numEdges; ++i)
			{
				if (isEdgeAlive(i))
				{
					return i;
				}
			}

			return numEdges;
		}

		uint32_t Mesh::getNextEdgeIndex(uint32_t edgeIndex) const
		{
			auto numEdges = (uint32_t)mEdges.size();
			for (uint32_t i = edgeIndex + 1; i < numEdges; ++i)
			{
				if (isEdgeAlive(i))
				{
					return i;
				}
			}

			return numEdges;
		}

		bool Mesh::edgeIndexIterationFinished(uint32_t edgeIndex) const
		{
			return edgeIndex == mEdges.size();
		}

		Edge const& Mesh::getEdge(uint32_t edgeIndex) const
		{
			ASSERT_TRACE(edgeIndex < mEdges.size() && "Mesh::getEdge(): index is out of range.");
			ASSERT_TRACE(mDeadEdges.find(edgeIndex) == mDeadEdges.end() && "Mesh::getEdge(): edge is on dead list.");

			return mEdges[edgeIndex];
		}

		Edge& Mesh::_getEdge(uint32_t edgeIndex)
		{
			ASSERT_TRACE(edgeIndex < mEdges.size() && "Mesh::_getEdge(): index is out of range.");
			ASSERT_TRACE(mDeadEdges.find(edgeIndex) == mDeadEdges.end() && "Mesh::_getEdge(): edge is on dead list.");

			return mEdges[edgeIndex];
		}

		int32_t Mesh::getEdgeIndexByVertices(int32_t firstVertex, int32_t secondVertex, bool* correctOrder) const
		{
			int32_t index = getEdgeIndexByOrderedVertices(firstVertex, secondVertex);
			
			if (index >= 0)
			{
				if (correctOrder)
				{
					*correctOrder = true;
				}
				return index;
			}
			else
			{
				if (correctOrder)
				{
					*correctOrder = false;
				}

				return getEdgeIndexByOrderedVertices(secondVertex, firstVertex);
			}
		}

		int32_t Mesh::getEdgeIndexByOrderedVertices(int32_t firstVertex, int32_t secondVertex) const
		{
			uint64_t vertexEdgeKey = getVertexEdgeKey(firstVertex, secondVertex);
			
			auto res = mVerticesToEdges.find(vertexEdgeKey);
			return (res == mVerticesToEdges.end()) ? -1 : res->second;
		}


		IndexVector Mesh::getEdgeIndicesByConnectivity(Edge::Connectivity type) const
		{
			IndexVector edgeIndices;

			uint32_t edgeIndex = getFirstEdgeIndex();
			while (!edgeIndexIterationFinished(edgeIndex))
			{
				if (getEdge(edgeIndex).getConnectivity() == type)
				{
					edgeIndices.push_back(edgeIndex);
				}

				// Get next edge
				edgeIndex = getNextEdgeIndex(edgeIndex);
			}

			return edgeIndices;
		}

		uint32_t Mesh::addPolygon(Polygon polygon, int32_t prototype)
		{
			// Order the edges
			polygon.reorderEdges(this);

			uint32_t index = (uint32_t)mPolygons.size();

			// Update edge references to this polygon.
			for (auto it = polygon.getFirstEdge(); it != polygon.getEndEdge(); ++it)
			{
				auto& edge = _getEdge((*it).index);
				edge.addPolygonReference(index);
			}

			mPolygons.push_back(polygon);

			auto& addedPolygon = mPolygons.back();

			addedPolygon.setMesh(this, index);
			addedPolygon.setDeleteFunction(&Mesh::_killPolygon);
			addedPolygon.setUpdateRefFunction(&Mesh::_updateEdgePolygonReferences);
			addedPolygon.mPublicId = mPolygonIdGenerator++;

		#ifdef CHECK_INTEGRITY
			checkIntegrity();
		#endif

			// Acceleration grid
			if (mPolygonAccelerationGrid)
			{
				mPolygonAccelerationGrid->addItem(index, addedPolygon.getBoundingBox());
			}

			// Callbacks
			for (auto const& callbacks: mCallbacks)
			{
				auto const& callback = callbacks.second;
				if (callback.onAddPolygon)
				{
					callback.onAddPolygon(index, prototype);
				}
			}

			return index;
		}

		void Mesh::addHoleToPolygon(uint32_t polygonIndex, uint32_t holeIndex)
		{
			auto& polygon = _getPolygon(polygonIndex);
			auto& hole = _getPolygon(holeIndex);
			
			hole.convertToHole();

			polygon.addHole(hole);
		}

		void Mesh::addFilledHoleToPolygon(uint32_t polygonIndex, uint32_t holeIndex, uint32_t* newFilledIndex)
		{
			auto& hole = _getPolygon(holeIndex);

			// Create new polygon from hole
			auto holeEdges = hole.getEdges();
			IndexVector edgeData;

			for (auto const& holeEdge: holeEdges)
			{
				edgeData.push_back(holeEdge.v0);
				edgeData.push_back(holeEdge.v1);
				edgeData.push_back(holeEdge.index);
			}

			Polygon filled(edgeData);
			uint32_t filledIndex = addPolygon(filled);

			// Set old polygon as hole
			auto holeIndices = getPolygon(holeIndex).getHoleIndices();

			_getPolygon(polygonIndex).addHole(_getPolygon(holeIndex));
			if (*newFilledIndex)
			{
				*newFilledIndex = filledIndex;
			}

			for (uint32_t hIndex: holeIndices)
			{
				addHoleToPolygon(filledIndex, hIndex);
			}
		}

		void Mesh::removeHoleFromPolygon(uint32_t polygonIndex, uint32_t holeIndex)
		{
			auto& polygon = _getPolygon(polygonIndex);
			polygon.removeHole(holeIndex);
		}

		void Mesh::removeHolesFromPolygon(uint32_t polygonIndex)
		{
			auto& polygon = _getPolygon(polygonIndex);
			auto holeIndices = polygon.getHoleIndices();

			for (uint32_t holeIndex: holeIndices)
			{
				polygon.removeHole(holeIndex);
			}
		}

		void Mesh::removePolygon(uint32_t polygonIndex, bool deleteHoles)
		{
			auto& polygon = _getPolygon(polygonIndex);

			// Remove holes first
			if (deleteHoles)
			{
				for (uint32_t holeIndex: polygon.getHoleIndices())
				{
					removePolygon(holeIndex);
				}
			}
			else
			{
				// Convert back to polygons
				for (uint32_t holeIndex: polygon.getHoleIndices())
				{
					auto& hole = _getPolygon(holeIndex);
					hole.convertFromHole();
				}
			}

			// Remove polygon by removing all its edges
			polygon.removeEdgesNotInSet({});

			// Update grid
			if (mPolygonAccelerationGrid)
			{
				mPolygonAccelerationGrid->removeItem(polygonIndex);
			}
		}

		void Mesh::removePolygons(IndexSet const& polygonIndices)
		{
			bool first = true;
			try
			{
				for (uint32_t polygonIndex: polygonIndices)
				{
					removePolygon(polygonIndex);
					first = false;
				}
			}
			catch (GeometryOperationException& e)
			{
				if (!first)
				{
					e.setConsistentState(false);
				}

				throw e;
			}
		}

		void Mesh::killPolygon(uint32_t polygonIndex)
		{
			ASSERT_TRACE(mDeadPolygons.find(polygonIndex) == mDeadPolygons.end() && "Mesh::killPolygon(): polygon is already marked as dead.");
			mDeadPolygons.insert(polygonIndex);

			// Acceleration grid
			if (mPolygonAccelerationGrid)
			{
				mPolygonAccelerationGrid->removeItem(polygonIndex);
			}

			// Callbacks
			for (auto const& callbacks: mCallbacks)
			{
				auto const& callback = callbacks.second;
				if (callback.onRemovePolygon)
				{
					callback.onRemovePolygon(polygonIndex);
				}
			}
		}

		void Mesh::movePolygon(uint32_t polygonIndex, Vector2 const& move)
		{
			auto& polygon = _getPolygon(polygonIndex);

			auto const& vertexIndices = polygon.getOrderedVertexIndices(nullptr);

			for (auto vertexIndex : vertexIndices)
			{
				moveVertex(vertexIndex, move);
			}
		}

		void Mesh::movePolygons(IndexVector const& polygonIndices, Vector2 const& move)
		{
			// Get unique vertices so we don't move the same one more than once.
			IndexSet uniqueVertices;

			for (auto polygonIndex: polygonIndices)
			{
				Polygon const& polygon = getPolygon(polygonIndex);
				auto const& polygonVertexIndices = polygon.getOrderedVertexIndices(nullptr);

				for (auto vertexIndex : polygonVertexIndices)
				{
					uniqueVertices.insert(vertexIndex);
				}
			}

			// Move them.
			IndexVector vertexList;
			vertexList.assign(uniqueVertices.begin(), uniqueVertices.end());

			return moveVertices(vertexList, move);
		}

		void Mesh::recentrePolygon(uint32_t polygonIndex, Vector2 const& centre)
		{
			auto& polygon = _getPolygon(polygonIndex);
			
			BoundingBox bb = polygon.getBoundingBox();
			movePolygon(polygonIndex, centre - bb.getCentre());
		}

		void Mesh::_mergePolygons(uint32_t targetIndex, uint32_t sourceIndex)
		{
			Polygon& targetPolygon = mPolygons[targetIndex];
			Polygon& sourcePolygon = mPolygons[sourceIndex];

			// Add source edges to target.
			while (sourcePolygon.getNumEdges() > 0)
			{
				auto it = sourcePolygon.getFirstEdge();

				auto const& directedEdge = *it;

				int firstIndex = directedEdge.v0;
				int secondIndex = directedEdge.v1;

				targetPolygon.addEdge(firstIndex, secondIndex, directedEdge.index);
				sourcePolygon.removeEdge(directedEdge.index, false);
			}

			targetPolygon.removeTwoSidedEdges();

			// Update acceleration grid
			if (mPolygonAccelerationGrid)
			{
				mPolygonAccelerationGrid->moveItem(targetIndex, targetPolygon.getBoundingBox());
			}
		}

		uint32_t Mesh::_mergePolygons(IndexSet const& polygonIndices)
		{
			auto indices = polygonIndices;

			uint32_t targetIndex = *indices.begin();
			indices.erase(targetIndex);

			while (!indices.empty())
			{
				uint32_t sourceIndex = *indices.begin();
				indices.erase(sourceIndex);

				_mergePolygons(targetIndex, sourceIndex);
			}

			return targetIndex;
		}

		void Mesh::_closePolygon(uint32_t polygonIndex, bool insertNotStitch, int32_t prototypeEdgeIndex, uint32_t* createdEdgeIndex)
		{
			pair<int, int> breakIndices;

			Polygon& polygon = _getPolygon(polygonIndex);
			IndexVector vertexIndices = polygon.getOrderedVertexIndices(&breakIndices);

			if (breakIndices.first < 0)
			{
				// No breaks, nothing to do.
				return;
			}

			if (insertNotStitch)
			{
				// Insert an edge between breakIndices
				Edge newEdge(breakIndices.first, breakIndices.second);

				uint32_t newEdgeIndex = addEdgeImpl(newEdge, prototypeEdgeIndex);
				polygon.addEdge(breakIndices.first, breakIndices.second, newEdgeIndex);

				if (createdEdgeIndex)
				{
					*createdEdgeIndex = newEdgeIndex;
				}
			}
			else
			{
				// Modify the neighbour edges to join them up.  To work out which one to weld to which:
				// If both vertices have the same number of edge refs, then move first vertex to midpoint,
				// and weld second vertex to it.  Otherwise weld the one with fewer edge refs to the one
				// with more.
				auto& firstVertex = _getVertex(breakIndices.first);
				auto& secondVertex = _getVertex(breakIndices.second);
				uint32_t firstEdgeCount = (uint32_t)firstVertex.getEdgeReferences().size();
				uint32_t secondEdgeCount = (uint32_t)secondVertex.getEdgeReferences().size();

				if (firstEdgeCount == secondEdgeCount)
				{
					Vector2 move = (secondVertex.getPosition() - firstVertex.getPosition()) / 2.0f;
					moveVertex(breakIndices.first, move);

					mergeVertices(breakIndices.first, breakIndices.second);
				}
				else if (firstEdgeCount < secondEdgeCount)
				{
					mergeVertices(breakIndices.second, breakIndices.first);
				}
				else
				{
					mergeVertices(breakIndices.first, breakIndices.second);
				}

				if (createdEdgeIndex)
				{
					*createdEdgeIndex = (uint32_t)-1;
				}
			}
		}

		vector<pair<uint32_t, float>> Mesh::linePolygonIntersection(uint32_t polyIndex, Vector2 const& v0, Vector2 const& v1)
		{
			auto const& polygon = getPolygon(polyIndex);
			auto edgeIndexList = polygon.getEdgeIndexList();

			vector<pair<uint32_t, float>> intersections;
			for (uint32_t edgeIndex: edgeIndexList)
			{
				auto const& edge = getEdge(edgeIndex);
				Vector2 ev0 = getVertex(edge.getFirstVertex()).getPosition();
				Vector2 ev1 = getVertex(edge.getSecondVertex()).getPosition();

				LineHit hit;
				if (MathsUtils::lineLineIntersection(ev0, ev1, v0, v1, &hit)
					== MathsUtils::LineIntersectionType::Intersecting)
				{
					intersections.push_back(make_pair(edgeIndex, hit.getTime()));
				}
			}

			return intersections;
		}

		uint32_t Mesh::getNumPolygons() const
		{
			return (uint32_t)(mPolygons.size() - mDeadPolygons.size());
		}

		uint32_t Mesh::getFirstPolygonIndex() const
		{
			auto numPolygons = (uint32_t)mPolygons.size();
			for (uint32_t i = 0; i < numPolygons; ++i)
			{
				if (isPolygonAlive(i))
				{
					return i;
				}
			}

			return numPolygons;
		}

		uint32_t Mesh::getNextPolygonIndex(uint32_t polygonIndex) const
		{
			auto numPolygons = (uint32_t)mPolygons.size();
			for (uint32_t i = polygonIndex + 1; i < numPolygons; ++i)
			{
				if (isPolygonAlive(i))
				{
					return i;
				}
			}

			return numPolygons;
		}

		bool Mesh::polygonIndexIterationFinished(uint32_t polygonIndex) const
		{
			return polygonIndex == mPolygons.size();
		}

		Polygon const& Mesh::getPolygon(uint32_t polygonIndex) const
		{
			ASSERT_TRACE(polygonIndex < mPolygons.size() && "Mesh::getPolygon(): index is out of range.");
			ASSERT_TRACE(mDeadPolygons.find(polygonIndex) == mDeadPolygons.end() && "Mesh::getPolygon(): polygon is on dead list.");

			return mPolygons[polygonIndex];
		}

		Polygon& Mesh::_getPolygon(uint32_t polygonIndex)
		{
			ASSERT_TRACE(polygonIndex < mPolygons.size() && "Mesh::_getPolygon(): index is out of range.");
			ASSERT_TRACE(mDeadPolygons.find(polygonIndex) == mDeadPolygons.end() && "Mesh::_getPolygon(): polygon is on dead list.");

			return mPolygons[polygonIndex];
		}

		bool Mesh::isVertexAlive(uint32_t vertexIndex) const
		{
			return !(mDeadVertices.find(vertexIndex) != mDeadVertices.end());
		}

		bool Mesh::isVertexCollinearOrphan(uint32_t vertexIndex, float vertexTolerance) const
		{
			Vertex const& vertex = getVertex(vertexIndex);
			Vector2 vertexPos = vertex.getPosition();

			auto edgeRefs = vertex.getEdgeReferences();

			if (edgeRefs.size() == 2)
			{
				auto const& edge0 = getEdge(*edgeRefs.begin());
				auto const& edge1 = getEdge(*--edgeRefs.end());

				uint32_t neighbour0, neighbour1;
				neighbour0 = (uint32_t)edge0.getFirstVertex() == vertexIndex ? (uint32_t)edge0.getSecondVertex() : (uint32_t)edge0.getFirstVertex();
				neighbour1 = (uint32_t)edge1.getFirstVertex() == vertexIndex ? (uint32_t)edge1.getSecondVertex() : (uint32_t)edge1.getFirstVertex();

				Vector2 n0Pos = getVertex(neighbour0).getPosition();
				Vector2 n1Pos = getVertex(neighbour1).getPosition();

				float offset = vertexPos.distanceToLine(n0Pos, n1Pos);
				return offset < vertexTolerance;
			}
			else
			{
				return false;
			}
		}

		bool Mesh::isEdgeAlive(uint32_t edgeIndex) const
		{
			return !(mDeadEdges.find(edgeIndex) != mDeadEdges.end());
		}

		void Mesh::updateVertexEdgeReferences(uint32_t edgeIndex, uint32_t oldVertex0Index, uint32_t newVertex0Index, uint32_t oldVertex1Index, uint32_t newVertex1Index)
		{
			// Update vertex references
			if (oldVertex0Index != newVertex0Index)
			{
				auto& oldVertex = _getVertex(oldVertex0Index);
				oldVertex.removeEdgeReference(edgeIndex);

				auto& newVertex = _getVertex(newVertex0Index);
				newVertex.addEdgeReference(edgeIndex);
			}

			if (oldVertex1Index != newVertex1Index)
			{
				auto& oldVertex = _getVertex(oldVertex1Index);
				oldVertex.removeEdgeReference(edgeIndex);

				auto& newVertex = _getVertex(newVertex1Index);
				newVertex.addEdgeReference(edgeIndex);
			}

			// Update vertex-to-edge mapping
			removeEdgeFromReverseLookup(oldVertex0Index, oldVertex1Index);
			addEdgeToReverseLookup(edgeIndex, newVertex0Index, newVertex1Index);
		}

		uint64_t Mesh::addEdgeToReverseLookup(Edge const& edge, uint32_t edgeIndex)
		{
			return addEdgeToReverseLookup(edgeIndex, edge.getFirstVertex(), edge.getSecondVertex());
		}

		uint64_t Mesh::addEdgeToReverseLookup(uint32_t edgeIndex, uint32_t firstVertex, uint32_t secondVertex)
		{
			uint64_t edgeVertexKey = getVertexEdgeKey(firstVertex, secondVertex);
			auto res = mVerticesToEdges.insert(make_pair(edgeVertexKey, edgeIndex));
			ASSERT_TRACE(res.second && "Mesh::addEdgeToLookup(): edge already exists in reverse lookup.");

			return edgeVertexKey;
		}

		void Mesh::removeEdgeFromReverseLookup(Edge const& edge)
		{
			removeEdgeFromReverseLookup(edge.getFirstVertex(), edge.getSecondVertex());
		}

		void Mesh::removeEdgeFromReverseLookup(uint32_t firstVertex, uint32_t secondVertex)
		{
			uint64_t edgeVertexKey = getVertexEdgeKey(firstVertex, secondVertex);
#ifdef DEBUG
			size_t erased = mVerticesToEdges.erase(edgeVertexKey);
			ASSERT_TRACE(erased == 1 && "Mesh::removeEdgeFromReverseLookup(): edge does not exist in reverse lookup.");
#endif
		}

		uint64_t Mesh::getVertexEdgeKey(uint32_t firstVertex, uint32_t secondVertex) const
		{
			uint64_t secondVertex64 = secondVertex;
			return (uint64_t)(firstVertex + (secondVertex64 << 32));
		}

		bool Mesh::isPolygonAlive(uint32_t polygonIndex) const
		{
			return !(mDeadPolygons.find(polygonIndex) != mDeadPolygons.end());
		}

		void Mesh::mergeVertices(uint32_t targetIndex, uint32_t sourceIndex)
		{
			auto& sourceVertex = _getVertex(sourceIndex);
			auto& targetVertex = _getVertex(targetIndex);

			// Set all edges that use the source vertex to use the target vertex instead,
			// then delete source.  Get by value so as to not invalidate the iterator.
			IndexSet edgeRefs = sourceVertex.getEdgeReferences();
			for (auto edgeIndex: edgeRefs)
			{
				auto& edge = _getEdge(edgeIndex);

				uint32_t firstIndex = edge.getFirstVertex();
				uint32_t secondIndex = edge.getSecondVertex();

				if (firstIndex == sourceIndex)
				{
					edge.setFirstVertex(targetIndex);
					targetVertex.addEdgeReference(edgeIndex);

					// Update polygons that use this edge
					auto& polygonRefs = edge.getPolygonReferences();
					for (auto polygonRef: polygonRefs)
					{
						auto& polygon = _getPolygon(polygonRef);

						// Get ref to directed edge with first vertex of whatever the edge used to have,
						// and change it to targetVertex.
						auto edgesIt = polygon.getEdgesByFirstVertex(firstIndex);
						for (auto edgeIt: edgesIt)
						{
							polygon.updateEdge(edgeIt, targetIndex, (*edgeIt).v1, (*edgeIt).index);
						}
					}
				}
				else if (secondIndex == sourceIndex)
				{
					edge.setSecondVertex(targetIndex);
					targetVertex.addEdgeReference(edgeIndex);

					// Update polygons that use this edge
					auto& polygonRefs = edge.getPolygonReferences();
					for (auto polygonRef: polygonRefs)
					{
						auto& polygon = _getPolygon(polygonRef);

						// Get ref to directed edge with second vertex of whatever the edge used to have,
						// and change it to targetVertex.
						auto edgesIt = polygon.getEdgesBySecondVertex(secondIndex);
						for (auto edgeIt: edgesIt)
						{
							polygon.updateEdge(edgeIt, (*edgeIt).v0, targetIndex, (*edgeIt).index);
						}
					}
				}

				sourceVertex.removeEdgeReference(edgeIndex);
			}

		#ifdef CHECK_INTEGRITY
			checkIntegrity();
		#endif
		}

		void Mesh::mergeEdges(uint32_t targetIndex, uint32_t sourceIndex)
		{
			auto& targetEdge = _getEdge(targetIndex);
			auto const& sourceEdge = getEdge(sourceIndex);

			uint32_t t1 = targetEdge.getFirstVertex();
			uint32_t t2 = targetEdge.getSecondVertex();
			uint32_t s1 = sourceEdge.getFirstVertex();
			uint32_t s2 = sourceEdge.getSecondVertex();

			killEdge(sourceIndex);

			if (t1 == s1)
			{
				targetEdge.setFirstVertex(s2);
				killVertex(t1);
			}
			else if (t1 == s2)
			{
				targetEdge.setFirstVertex(t1);
				killVertex(t1);
			}
			else if (t2 == s1)
			{
				targetEdge.setSecondVertex(s2);
				killVertex(t2);
			}
			else if (t2 == s2)
			{
				targetEdge.setSecondVertex(s1);
				killVertex(t2);
			}

			// Update polygon refs.
			auto const& polyRefs = targetEdge.getPolygonReferences();
			for (auto polyRef: polyRefs)
			{
				auto& polygon = _getPolygon(polyRef);
				auto edgeIt = polygon.getEdgeByIndex(targetIndex);

				polygon.updateEdge(edgeIt, targetEdge.getFirstVertex(), targetEdge.getSecondVertex(), targetIndex);
			}
		}
		
		void Mesh::updateEdgePolygonReferences(uint32_t polygonIndex, int oldEdgeIndex, int newEdgeIndex, bool deleteIfOrphaned)
		{
			if (oldEdgeIndex >= 0)
			{
				auto& edge = _getEdge(oldEdgeIndex);
				edge._removePolygonReference(polygonIndex, deleteIfOrphaned);
			}

			if (newEdgeIndex >= 0)
			{
				auto& edge = _getEdge(newEdgeIndex);
				edge.addPolygonReference(polygonIndex);
			}
		}

		void Mesh::setIntegrityCheck(bool enable)
		{
			mCheckIntegrity.push(enable);
		}

		void Mesh::popIntegrityCheck()
		{
			mCheckIntegrity.pop();
		}

		void Mesh::checkIntegrity()
		{
			if (!integrityCheckEnabled())
			{
				return;
			}

			// Check that for all live vertices:
			// - they exist in the edges that they reference.
			for (uint32_t i = 0; i < mVertices.size(); ++i)
			{
				if (isVertexAlive(i))
				{
#ifdef _DEBUG
					auto const& vertex = getVertex(i);

					auto const& edgeRefs = vertex.getEdgeReferences();
					for (auto edgeRef : edgeRefs)
					{
						// Check that the edge that this vertex is referencing is alive.
						ASSERT_TRACE(isEdgeAlive(edgeRef) && "Mesh::checkIntegrity(): edge referenced by vertex is dead.");

						// Check that the edge that this vertex is referencing actually uses the vertex as one
						// of its vertices.

						auto const& edge = getEdge(edgeRef);
						ASSERT_TRACE((((uint32_t)edge.getFirstVertex() == i) || ((uint32_t)edge.getSecondVertex() == i)) && "Mesh::checkIntegrity(): edge referenced by vertex does not use the vertex.");
					}
#endif
				}
			}

			// Check that for all live edges:
			// - their vertices are alive and non-negative.
			// - their vertices are not the same.
			// - the vertices they use reference them.
			// - they are used by the polygons that they reference.

			// Check that duplicate edges do not exist.
			set<Edge, UndirectedEdgeComparer> uniqueEdges;

			uint32_t totalLiveEdges = 0;
			for (uint32_t i = 0; i < mEdges.size(); ++i)
			{
				if (isEdgeAlive(i))
				{
					auto const& edge = getEdge(i);

					uniqueEdges.insert(edge);
					totalLiveEdges++;
#ifdef _DEBUG
					int firstVertex = edge.getFirstVertex();
					int secondVertex = edge.getSecondVertex();

					// Check that the two vertices are not the same.
					ASSERT_TRACE((firstVertex != secondVertex) && "Mesh::checkIntegrity(): edge vertices are the same.");

					// Check that the vertices are initialised, ie >= 0.
					ASSERT_TRACE((firstVertex >= 0) && "Mesh::checkIntegrity(): edge uses vertex with index -1.");
					ASSERT_TRACE((secondVertex >= 0) && "Mesh::checkIntegrity(): edge uses vertex with index -1.");

					// Check that the vertices are alive.
					ASSERT_TRACE(isVertexAlive(firstVertex) && "Mesh::checkIntegrity(): edge uses dead vertex.");
					ASSERT_TRACE(isVertexAlive(secondVertex) && "Mesh::checkIntegrity(): edge uses dead vertex.");

					// Check that the vertices used reference the edge back in turn.

					auto const& firstVertexEdges = getVertex(firstVertex).getEdgeReferences();

					ASSERT_TRACE((firstVertexEdges.find(i) != firstVertexEdges.end()) && "Mesh::checkIntegrity(): vertex used by edge does not reference the edge.");

					auto const& secondVertexEdges = getVertex(secondVertex).getEdgeReferences();

					ASSERT_TRACE((secondVertexEdges.find(i) != secondVertexEdges.end()) && "Mesh::checkIntegrity(): vertex used by edge does not reference the edge.");

					auto const& polygonRefs = edge.getPolygonReferences();
					for (auto polygonRef : polygonRefs)
					{
						// Check that the polygons referenced by the edge are alive.
						ASSERT_TRACE(polygonRef >= 0 && "Mesh::checkIntegrity(): polygon referenced by edge has index -1.");
						ASSERT_TRACE(isPolygonAlive(polygonRef) && "Mesh::checkIntegrity(): polygon referenced by edge is dead.");

						auto const& polygon = getPolygon(polygonRef);
						auto const& polygonEdges = polygon.getEdgeIndexSet();

						// Check that the polygons referenced by the edge use the edge.
						ASSERT_TRACE((polygonEdges.find(i) != polygonEdges.end()) && "Mesh::checkIntegrity(): polygon referenced by edge does not use the edge.");
					}
#endif
				}
			}

			ASSERT_TRACE(uniqueEdges.size() == totalLiveEdges && "Mesh::checkIntegrity(): duplicate edges exist.");

			// Check that for all live polygons:
			// - they have at least 3 edges.
			// - their ordered vertices have no breaks.
			// - the edges they use reference them in turn.
			// - the vertices in their DirectedEdges are the right ones for the edges
			for (uint32_t i = 0; i < mPolygons.size(); ++i)
			{
				if (isPolygonAlive(i))
				{
					auto const& polygon = getPolygon(i);
					auto const& edgeIndices = polygon.getEdgeIndexSet();

					// Check there are no breaks.
					pair<int, int> breakIndices;

					IndexVector vertexIndices = polygon.getOrderedVertexIndices(&breakIndices);

					ASSERT_TRACE((breakIndices.first < 0 && breakIndices.second < 0) && "Mesh::checkIntegrity(): break exists in polygon.");

					// Check that the used edges reference the polygon in turn.
#ifdef _DEBUG
					for (auto edgeIndex: edgeIndices)
					{
						// Check that the edges referenced by the polygon are alive.
						ASSERT_TRACE(edgeIndex >= 0 && "Mesh::checkIntegrity(): edge used by polygon has index -1.");
						ASSERT_TRACE(isEdgeAlive(edgeIndex) && "Mesh::checkIntegrity(): edge used by polygon is dead.");


						auto const& edge = getEdge(edgeIndex);
						auto const& polygonRefs = edge.getPolygonReferences();

						// Check that the edges used by the polygon reference the polygon.
						ASSERT_TRACE((polygonRefs.find(i) != polygonRefs.end()) && "Mesh::checkIntegrity(): edge used by polygon does not reference the polygon.");
					}
#endif

					// Check DirectedEdges
					auto const& directedEdges = polygon.getEdges();
					IndexSet usedEdges;
					for (auto const& de: directedEdges)
					{
						// Check for double-sided edges
						ASSERT_TRACE(usedEdges.find(de.index) == usedEdges.end() && "Mesh::checkIntegrity(): polygon uses an edge twice.");
						usedEdges.insert(de.index);
					}
				}
			}
		}

		uint32_t Mesh::addMeshCallbacks(MeshCallbacks const& callbacks)
		{
			uint32_t index = (uint32_t)mCallbacks.size();

			mCallbacks.insert(make_pair(index, callbacks));
			return index;
		}

		void Mesh::removeMeshCallbacks(uint32_t id)
		{
			auto it = mCallbacks.find(id);
			
			if (it != mCallbacks.end())
			{
				mCallbacks.erase(it);
			}
		}

		void Mesh::getExtents(Vector2& minExtent, Vector2& maxExtent) const
		{
			if (mRecalculateExtents)
			{
				recalculateExtents();
			}

			minExtent = mMinExtent;
			maxExtent = mMaxExtent;
		}

		void Mesh::createAccelerationGrids(float x, float y, float sizeX, float sizeY, int dimX, int dimY)
		{
			//
			// Vertex acceleration grid
			//
			if (mVertexAccelerationGrid)
			{
				delete mVertexAccelerationGrid;
			}

			mVertexAccelerationGrid = new AccelerationGrid(x, y, sizeX, sizeY, dimX, dimY);

			// Add everything to it.
			uint32_t vertexIndex = getFirstVertexIndex();
			while (!vertexIndexIterationFinished(vertexIndex))
			{
				mVertexAccelerationGrid->addItem(vertexIndex, getVertex(vertexIndex).getBoundingBox());
				vertexIndex = getNextVertexIndex(vertexIndex);
			}

			//
			// Edge acceleration grid
			//
			if (mEdgeAccelerationGrid)
			{
				delete mEdgeAccelerationGrid;
			}

			mEdgeAccelerationGrid = new AccelerationGrid(x, y, sizeX, sizeY, dimX, dimY);

			// Add everything to it.
			uint32_t edgeIndex = getFirstEdgeIndex();
			while (!edgeIndexIterationFinished(edgeIndex))
			{
				mEdgeAccelerationGrid->addItem(edgeIndex, getEdge(edgeIndex).getBoundingBox());
				edgeIndex = getNextEdgeIndex(edgeIndex);
			}

			//
			// Polygon acceleration grid
			//
			if (mPolygonAccelerationGrid)
			{
				delete mPolygonAccelerationGrid;
			}

			mPolygonAccelerationGrid = new AccelerationGrid(x, y, sizeX, sizeY, dimX, dimY);

			// Add everything to it.
			uint32_t polygonIndex = getFirstPolygonIndex();
			while (!polygonIndexIterationFinished(polygonIndex))
			{
				mPolygonAccelerationGrid->addItem(polygonIndex, getPolygon(polygonIndex).getBoundingBox());
				polygonIndex = getNextPolygonIndex(polygonIndex);
			}
		}

		AccelerationGrid const* Mesh::_getVertexAccelerationGrid() const
		{
			return mVertexAccelerationGrid;
		}

		AccelerationGrid const* Mesh::_getEdgeAccelerationGrid() const
		{
			return mEdgeAccelerationGrid;
		}

		AccelerationGrid const* Mesh::_getPolygonAccelerationGrid() const
		{
			return mPolygonAccelerationGrid;
		}

		void Mesh::merge(Mesh const* other)
		{
			// Add vertices
			int32_t vertexBase = -1;
			uint32_t vertexIndex = other->getFirstVertexIndex();
			while (other->vertexIndexIterationFinished(vertexIndex))
			{
				uint32_t vertexId = addVertex(other->getVertex(vertexIndex));

				if (vertexBase == -1)
				{
					vertexBase = (int32_t)vertexId;
				}

				vertexIndex = other->getNextVertexIndex(vertexIndex);
			}

			// Add edges
			int32_t edgeBase = -1;
			uint32_t edgeIndex = other->getFirstEdgeIndex();
			while (other->edgeIndexIterationFinished(edgeIndex))
			{
				Edge edge = other->getEdge(edgeIndex);
				
				edge.setFirstVertex(edge.getFirstVertex() + vertexBase);
				edge.setSecondVertex(edge.getSecondVertex() + vertexBase);

				uint32_t edgeId = addEdge(edge);

				if (edgeBase == -1)
				{
					edgeBase = (int32_t)edgeId;
				}

				edgeIndex = other->getNextEdgeIndex(edgeIndex);
			}

			// Add polygons
			uint32_t polygonIndex = other->getFirstPolygonIndex();
			while (other->polygonIndexIterationFinished(polygonIndex))
			{
				Polygon polygon = other->getPolygon(polygonIndex);

				polygonIndex = other->getNextPolygonIndex(polygonIndex);
			}
		}

		int32_t Mesh::getContainingPolygon(Vector2 const& position) const
		{
			auto index = getFirstPolygonIndex();
			while (!polygonIndexIterationFinished(index))
			{
				auto const& polygon = getPolygon(index);
				if (polygon.pointInside(position))
				{
					return (int32_t)index;
				}

				index = getNextPolygonIndex(index);
			}

			return -1;
		}

		bool Mesh::pointInside(Vector2 const& position) const
		{
			return getContainingPolygon(position) >= 0;
		}

		bool Mesh::lineIntersects(Vector2 const& l0, Vector2 const& l1) const
		{
			uint32_t edgeIndex = getFirstEdgeIndex();
			while (!edgeIndexIterationFinished(edgeIndex))
			{
				auto const& edge = getEdge(edgeIndex);
				auto const& vertex0 = getVertex(edge.getFirstVertex());
				auto const& vertex1 = getVertex(edge.getSecondVertex());

				auto is = MathsUtils::lineLineIntersection(l0, l1, vertex0.getPosition(), vertex1.getPosition());
				if (is != MathsUtils::LineIntersectionType::NotIntersecting && is != MathsUtils::LineIntersectionType::Touching)
				{
					return true;
				}

				edgeIndex = getNextEdgeIndex(edgeIndex);
			}

			return false;
		}

		void Mesh::compact(map<uint32_t, uint32_t>* vertexRemapping, map<uint32_t, uint32_t>* edgeRemapping, map<uint32_t, uint32_t>* polygonRemapping)
		{
			//
			// Compact vertices
			//

			// Get new mappings
			int newLiveOffset = 0;
			vector<int32_t> newVertexIndices(mVertices.size(), -1);
			set<int32_t> vertexAttributeIndices;

			for (uint32_t i = 0; i < mVertices.size(); ++i)
			{
				if (mDeadVertices.find(i) == mDeadVertices.end())
				{
					if (i != (uint32_t)newLiveOffset)
					{
						auto& compactedVertex = mVertices[newLiveOffset];
						compactedVertex = mVertices[i];

						compactedVertex.setMesh(this, newLiveOffset);
						compactedVertex.setDeleteFunction(&Mesh::_killVertex);
						compactedVertex.setUpdateEdgeFunction(&Mesh::_updateVertexEdgeReference);

						// Callbacks
						for (auto const& callbacks: mCallbacks)
						{
							auto const& callback = callbacks.second;
							if (callback.onUpdateVertex)
							{
								callback.onUpdateVertex(i, newLiveOffset);
							}
						}
					}

					if (vertexRemapping)
					{
						vertexRemapping->insert(make_pair(i, newLiveOffset));
					}

					if (mVertices[newLiveOffset].mAttributeIndex >= 0)
					{
						vertexAttributeIndices.insert(mVertices[newLiveOffset].mAttributeIndex);
					}

					newVertexIndices[i] = newLiveOffset;
					newLiveOffset++;
				}
			}

			// Clear unused live vertices
			for (size_t i = 0; i < mDeadVertices.size(); ++i)
			{
				mVertices.pop_back();
			}

			// Clear dead vertices list
			mDeadVertices.clear();

			// Remap vertex attributes
			if (mVertexAttributes)
			{
				auto remappedVertexAttribs = mVertexAttributes->compact(vertexAttributeIndices);

				for (uint32_t i = 0; i < mVertices.size(); ++i)
				{
					auto& vertex = mVertices[i];

					auto newIndex = remappedVertexAttribs.find(vertex.mAttributeIndex);
					if (newIndex != remappedVertexAttribs.end())
					{
						vertex.mAttributeIndex = newIndex->second;
					}
				}
			}

			//
			// Compact edges
			//
			mVerticesToEdges.clear();

			// Get new mappings
			newLiveOffset = 0;
			vector<int> newEdgeIndices(mEdges.size(), -1);
			set<int32_t> edgeAttributeIndices;

			for (uint32_t i = 0; i < mEdges.size(); ++i)
			{
				if (mDeadEdges.find(i) == mDeadEdges.end())
				{
					if (i != (uint32_t)newLiveOffset)
					{
						Edge& compactedEdge = mEdges[newLiveOffset];
						compactedEdge = mEdges[i];

						compactedEdge.setMesh(this, newLiveOffset);
						compactedEdge.setDeleteFunction(&Mesh::_killEdge);

						// Update edge vertices.  Disable the ref-updating here because the old vertex indices
						// are now invalid.
						compactedEdge.setUpdateRefFunction({});
						compactedEdge.setFirstVertex(newVertexIndices[compactedEdge.getFirstVertex()]);
						compactedEdge.setSecondVertex(newVertexIndices[compactedEdge.getSecondVertex()]);

						// Update the edge-vertex lookup
						addEdgeToReverseLookup(compactedEdge, newLiveOffset);

						compactedEdge.setUpdateRefFunction(&Mesh::_updateVertexEdgeReferences);

						// Callbacks
						for (auto const& callbacks: mCallbacks)
						{
							auto const& callback = callbacks.second;
							if (callback.onUpdateEdge)
							{
								callback.onUpdateEdge(i, newLiveOffset);
							}
						}
					}

					if (edgeRemapping)
					{
						edgeRemapping->insert(make_pair(i, newLiveOffset));
					}

					if (mEdges[newLiveOffset].mAttributeIndex >= 0)
					{
						edgeAttributeIndices.insert(mEdges[newLiveOffset].mAttributeIndex);
					}

					newEdgeIndices[i] = newLiveOffset;
					newLiveOffset++;
				}
			}

			// Clear unused live edges
			for (size_t i = 0; i < mDeadEdges.size(); ++i)
			{
				mEdges.pop_back();
			}

			// Clear dead edges list
			mDeadEdges.clear();

			// Update vertex edge references.  Take a copy so that we don't invalidate
			// the iterator as we remove them from the vertex.
			for (auto it = mVertices.begin(); it != mVertices.end(); ++it)
			{
				Vertex& vertex = *it;
				IndexSet edgeRefs = vertex.getEdgeReferences();
		
				for (auto ref: edgeRefs)
				{
					vertex.updateEdgeReference(ref, newEdgeIndices[ref]);
				}
			}

			// Remap polygon attributes
			if (mEdgeAttributes)
			{
				auto remappedEdgeAttribs = mEdgeAttributes->compact(edgeAttributeIndices);

				for (uint32_t i = 0; i < mEdges.size(); ++i)
				{
					auto& edge = mEdges[i];

					auto newIndex = remappedEdgeAttribs.find(edge.mAttributeIndex);
					if (newIndex != remappedEdgeAttribs.end())
					{
						edge.mAttributeIndex = newIndex->second;
					}
				}
			}

			//
			// Compact polygons
			//
			// Get new mappings
			newLiveOffset = 0;
			vector<int> newPolygonIndices(mPolygons.size(), -1);
			set<int32_t> polygonAttributeIndices;

			for (uint32_t i = 0; i < mPolygons.size(); ++i)
			{
				if (mDeadPolygons.find(i) == mDeadPolygons.end())
				{
					Polygon& compactedPolygon = mPolygons[newLiveOffset];

					if (i != (uint32_t)newLiveOffset)
					{
						compactedPolygon = mPolygons[i];
						compactedPolygon.setMesh(this, newLiveOffset);

						// Callbacks
						for (auto const& callbacks: mCallbacks)
						{
							auto const& callback = callbacks.second;
							if (callback.onUpdatePolygon)
							{
								callback.onUpdatePolygon(i, newLiveOffset);
							}
						}
					}

					newPolygonIndices[i] = newLiveOffset;

					// Update polygon edges.  Disable the ref-updating as the old indices will be invalid.
					compactedPolygon.setUpdateRefFunction({});

					for (auto it = compactedPolygon.getFirstEdge(); it != compactedPolygon.getEndEdge(); ++it)
					{
						auto const& edge = *it;
						compactedPolygon.updateEdge(it, newVertexIndices[edge.v0], newVertexIndices[edge.v1], newEdgeIndices[edge.index]);
					}
						
					compactedPolygon.setUpdateRefFunction(&Mesh::_updateEdgePolygonReferences);

					if (polygonRemapping)
					{
						polygonRemapping->insert(make_pair(i, newLiveOffset));
					}

					if (mPolygons[newLiveOffset].mAttributeIndex >= 0)
					{
						polygonAttributeIndices.insert(mPolygons[newLiveOffset].mAttributeIndex);
					}

					newLiveOffset++;
				}
			}

			// Clear unused live edges
			for (size_t i = 0; i < mDeadPolygons.size(); ++i)
			{
				mPolygons.pop_back();
			}

			// Update holes
			for (auto& polygon: mPolygons)
			{
				for (uint32_t& index: polygon.mHoleIndices)
				{
					index = newPolygonIndices[index];
				}
			}

			// Clear dead edges list
			mDeadPolygons.clear();

			// Update edge polygon references.  Take a copy so that we don't invalidate
			// the iterator as we remove them from the edge.
			for (auto it = mEdges.begin(); it != mEdges.end(); ++it)
			{
				Edge& edge = *it;
				IndexSet polygonRefs = edge.getPolygonReferences();
		
				for (auto ref: polygonRefs)
				{
					edge.updatePolygonReference(ref, newPolygonIndices[ref]);
				}
			}

			// Remap polygon attributes
			if (mPolygonAttributes)
			{
				auto remappedPolygonAttribs = mPolygonAttributes->compact(polygonAttributeIndices);

				for (uint32_t i = 0; i < mPolygons.size(); ++i)
				{
					auto& polygon = mPolygons[i];

					auto newIndex = remappedPolygonAttribs.find(polygon.mAttributeIndex);
					if (newIndex != remappedPolygonAttribs.end())
					{
						polygon.mAttributeIndex = newIndex->second;
					}
				}
			}

		#ifdef CHECK_INTEGRITY
			checkIntegrity();
		#endif
		}

		MathsUtils::LineIntersectionType Mesh::edgeEdgeIntersection(uint32_t edge0, uint32_t edge1, Vector2* point) const
		{
			// Make sure they aren't dead
			if (!isEdgeAlive(edge0) || !isEdgeAlive(edge1))
			{
				return MathsUtils::LineIntersectionType::NotIntersecting;
			}

			uint32_t v00index = getEdge(edge0).getFirstVertex();
			uint32_t v01index = getEdge(edge0).getSecondVertex();
			uint32_t v10index = getEdge(edge1).getFirstVertex();
			uint32_t v11index = getEdge(edge1).getSecondVertex();

			IndexSet indices;
			indices.insert(v00index);
			indices.insert(v01index);
			indices.insert(v10index);
			indices.insert(v11index);

			// If they share one vertex, then they are touching, not intersecting.
			if (indices.size() == 3)
			{
				return MathsUtils::LineIntersectionType::Touching;
			}

			// If they share both vertices, then they are equal.
			if (indices.size() == 2)
			{
				return MathsUtils::LineIntersectionType::Coincident;
			}

			// Else test for intersection.
			auto const& vertex00 = getVertex(v00index);
			auto const& vertex01 = getVertex(v01index);
			auto const& vertex10 = getVertex(v10index);
			auto const& vertex11 = getVertex(v11index);

			LineHit hit;
			MathsUtils::LineIntersectionType res = MathsUtils::lineLineIntersection(
				vertex00.getPosition(),
				vertex01.getPosition(),
				vertex10.getPosition(),
				vertex11.getPosition(),
				&hit);

			*point = hit.getPosition();
			return res;
		}

		IndexSet Mesh::getVertexIndicesInBoundingBox(BoundingBox const& box) const
		{
			IndexSet indices;

			uint32_t vertexIndex = getFirstVertexIndex();
			while (!vertexIndexIterationFinished(vertexIndex))
			{
				auto const& vertex = getVertex(vertexIndex);

				if (box.pointInside(vertex.getPosition()))
				{
					indices.insert(vertexIndex);
				}

				vertexIndex = getNextVertexIndex(vertexIndex);
			}

			return indices;
		}

		IndexSet Mesh::getVertexIndicesInBoundingCircle(BoundingCircle const& circle) const
		{
			IndexSet indices;

			uint32_t vertexIndex = getFirstVertexIndex();
			while (!vertexIndexIterationFinished(vertexIndex))
			{
				auto const& vertex = getVertex(vertexIndex);

				if (circle.pointInside(vertex.getPosition()))
				{
					indices.insert(vertexIndex);
				}

				vertexIndex = getNextVertexIndex(vertexIndex);
			}

			return indices;

		}

		void Mesh::print(ostream& out)
		{
			out << endl;
			out << "------------------------------------------" << endl;
			out << endl;

			// Print vertices
			out << "Vertices" << endl;
			out << endl;

			for (uint32_t i = 0; i < mVertices.size(); ++i)
			{
				Vertex const& vertex = mVertices[i];

				out << i << ": " << vertex.getPosition().x << ", " << vertex.getPosition().y << (!isVertexAlive(i) ? " (dead)" : "") << endl;

				out << "  Refs:";
				auto const& edgeRefs = vertex.getEdgeReferences();
				for (auto ref : edgeRefs)
				{
					out << " " << ref << ",";
				}
				out << endl;
			}

			out << endl;

			// Print edges
			out << "Edges" << endl;
			out << endl;

			for (uint32_t i = 0; i < mEdges.size(); ++i)
			{
				Edge const& edge = mEdges[i];

				out << i << ": " << edge.getFirstVertex() << ", " << edge.getSecondVertex() << (!isEdgeAlive(i) ? " (dead)" : "") << endl;

				out << "  Refs:";
				auto const& polygonRefs = edge.getPolygonReferences();
				for (auto ref : polygonRefs)
				{
					out << " " << ref << ",";
				}
				out << endl;
			}

			out << endl;

			// Print polygons
			out << "Polygons" << endl;
			out << endl;

			for (uint32_t i = 0; i < mPolygons.size(); ++i)
			{
				Polygon const& polygon = mPolygons[i];

				out << i << ": " << (!isPolygonAlive(i) ? " (dead)" : "") << endl;

				for (auto it = polygon.getFirstEdge(); it != polygon.getEndEdge(); ++it)
				{
					auto const& edge = *it;
					out << edge.v0 << " -> " << edge.v1 << " [" << edge.index << "]" << endl;
				}
			}
		}

		bool Mesh::integrityCheckEnabled() const
		{
			return mCheckIntegrity.top();
		}

	} // geometry
} // WP_NAMESPACE
