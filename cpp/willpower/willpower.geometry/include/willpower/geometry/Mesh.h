#pragma once

#include <cassert>
#include <vector>
#include <set>
#include <iostream>
#include <iterator>
#include <stack>

#include "willpower/common/Vector2.h"
#include "willpower/common/MathsUtils.h"
#include "willpower/common/BoundingBox.h"
#include "willpower/common/BoundingCircle.h"
#include "willpower/common/AccelerationGrid.h"
#include "willpower/common/Logger.h"
#include "willpower/common/Renderable.h"

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Vertex.h"
#include "willpower/geometry/Edge.h"
#include "willpower/geometry/Polygon.h"
#include "willpower/geometry/MeshCallbacks.h"
#include "willpower/geometry/Exception.h"
#include "willpower/geometry/Types.h"
#include "willpower/geometry/MeshOperationOptions.h"
#include "willpower/geometry/MeshOperationResults.h"
#include "willpower/geometry/UserAttributes.h"

#define CHECK_INTEGRITY

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class MeshQuery;

		class WP_GEOMETRY_API Mesh : public Renderable
		{
			friend class MeshQuery;
			friend class MeshOperations;

		public:

			typedef std::function<void(Mesh*, void*)> RenderCallback;

			typedef std::function<bool(Mesh const*, uint32_t)> EdgeFilterFunction;

		private:

			Logger* mLogger;

			// User data
			RenderCallback mRenderCallback;

			UserAttributesFactory 
				*mVertexAttributeFactory, 
				*mEdgeAttributeFactory, 
				*mPolygonAttributeFactory,
				*mPolygonVertexAttributeFactory;

			UserAttributesBase 
				*mVertexAttributes, 
				*mEdgeAttributes, 
				*mPolygonAttributes,
				*mPolygonVertexAttributes;

			// Primitive lists
			std::vector<Vertex> mVertices;
			IndexSet mDeadVertices;

			std::vector<Edge> mEdges;
			IndexSet mDeadEdges;

			std::vector<Polygon> mPolygons;
			IndexSet mDeadPolygons;

			// Reverse lookups
			std::map<uint64_t, uint32_t> mVerticesToEdges;

			// Callbacks
			std::map<uint32_t, MeshCallbacks> mCallbacks;

			// Query acceleration structures
			AccelerationGrid* mVertexAccelerationGrid;
			AccelerationGrid* mEdgeAccelerationGrid;
			AccelerationGrid* mPolygonAccelerationGrid;

			// Extents
			mutable Vector2 mMinExtent, mMaxExtent;

			mutable bool mRecalculateExtents;

			// Item Ids
			int32_t mVertexIdGenerator;
			int32_t mEdgeIdGenerator;
			int32_t mPolygonIdGenerator;

			// Checks
			std::stack<bool> mCheckIntegrity;

		private:

			void copyFrom(Mesh const& other);

			void recalculateExtents() const;

			//
			// Vertices
			//

			// Get mutable reference to the vertex.
			Vertex& _getVertex(uint32_t vertexIndex);

			// Destroy the vertex, and propagate this change to anything that it uses.
			void killVertex(uint32_t vertexIndex);

			// Check whether the vertex is alive.
			bool isVertexAlive(uint32_t vertexIndex) const;

			// Check whether a vertex has only two edges, and is directly
			// in line with the neighbouring vertices' position.
			bool isVertexCollinearOrphan(uint32_t vertexIndex, float vertexTolerance = -1.0f) const;

			//
			// Edges
			//

			// Function to add an edge when the mesh is not internally consistent.
			uint32_t addEdgeImpl(Edge const& edge, int32_t prototype);

			// Get mutable reference to the edge.
			Edge& _getEdge(uint32_t edgeIndex);

			// Destroy the edge, and propagate this change to anything that it uses.
			void killEdge(uint32_t edgeIndex);

			// Check whether the edge is alive.
			bool isEdgeAlive(uint32_t edgeIndex) const;

			// Callback used by edges to update the vertices that refer to them when they change
			// their vertices.
			void updateVertexEdgeReferences(uint32_t edgeIndex, uint32_t oldVertex0Index, uint32_t newVertex0Index, 
				uint32_t oldVertex1Index, uint32_t newVertex1Index);

			uint64_t addEdgeToReverseLookup(Edge const& edge, uint32_t edgeIndex);

			uint64_t addEdgeToReverseLookup(uint32_t edgeIndex, uint32_t firstVertex, uint32_t secondVertex);

			void removeEdgeFromReverseLookup(Edge const& edge);

			void removeEdgeFromReverseLookup(uint32_t firstVertex, uint32_t secondVertex);

			uint64_t getVertexEdgeKey(uint32_t firstVertex, uint32_t secondVertex) const;

			//
			// Polygons
			//

			// Get mutable reference to the polygon.
			Polygon& _getPolygon(uint32_t polygonIndex);

			// Destroy the polygon, and propagate this change to anything that it uses.
			void killPolygon(uint32_t polygonIndex);

			// Check whether the polygon is alive.
			bool isPolygonAlive(uint32_t polygonIndex) const;

			void _mergePolygons(uint32_t targetIndex, uint32_t sourceIndex);

			uint32_t _mergePolygons(IndexSet const& polygonIndices);

			void _closePolygon(uint32_t polygonIndex, bool insertNotStitch, int32_t prototypeEdgeIndex, uint32_t* createdEdgeIndex = nullptr);

			// Merge source vertex to target vertex by transferring properties from source to
			// target, then destroying source.
			void mergeVertices(uint32_t targetIndex, uint32_t sourceIndex);

			// Merge source edge into target edge, by deleting target edge and bridging
			// source edge to where it was.
			void mergeEdges(uint32_t targetIndex, uint32_t sourceIndex);

			// Callback used by polygons to update the edges that refer to them when they change
			// their edges.
			void updateEdgePolygonReferences(uint32_t polygonIndex, int oldEdgeIndex, int newEdgeIndex, bool deleteIfOrphaned);

			std::vector<std::pair<uint32_t, float>> linePolygonIntersection(uint32_t polygonIndex, Vector2 const& v0, Vector2 const& v1);

			void extrudePolygonEdgesDirected(uint32_t polygonIndex, DirectedEdgeVector const& edgeData, Vector2 const& extrusion, ExtrudePolygonOptions options, uint32_t* endVertices, uint32_t* collinearVertices, uint32_t* newPolygonIndex, uint32_t* holeIndex, IndexVector* extrudedEdgeIndices, IndexVector* sourceEdgeIndices);

			void extrudePolygonEdgesNormal(uint32_t polygonIndex, DirectedEdgeVector const& edgeData, float distance, ExtrudePolygonOptions options, uint32_t* endVertices, uint32_t* collinearVertices, uint32_t* newPolygonIndex, uint32_t* holeIndex, IndexVector* extrudedEdgeIndices, IndexVector* sourceEdgeIndices);

			std::vector<DirectedEdgeVector> getExtrusionSections(uint32_t polygonIndex, IndexVector const& edgeIndices, bool separate, bool allowLoop);

			// Check integrity of system.
			void checkIntegrity();

			// Sub-object handlers
			static void _killVertex(Mesh* mesh, uint32_t vertexIndex)
			{
				mesh->killVertex(vertexIndex);
			}

			static void _updateVertexEdgeReference(Mesh* mesh, uint32_t edgeIndex)
			{
				auto& edge = mesh->_getEdge(edgeIndex);
				edge.updateInternals();
			}

			static void _killEdge(Mesh* mesh, uint32_t edgeEndex)
			{
				mesh->killEdge(edgeEndex);
			}

			static void _updateVertexEdgeReferences(Mesh* mesh, uint32_t edgeIndex, uint32_t oldVertex0Index, 
				uint32_t newVertex0Index, uint32_t oldVertex1Index, uint32_t newVertex1Index)
			{
				mesh->updateVertexEdgeReferences(edgeIndex, oldVertex0Index, newVertex0Index, oldVertex1Index, newVertex1Index);
			}

			static void _killPolygon(Mesh* mesh, uint32_t polygonIndex)
			{
				mesh->killPolygon(polygonIndex);
			}

			static void _updateEdgePolygonReferences(Mesh* mesh, uint32_t polygonIndex, int oldEdgeIndex, int newEdgeIndex, bool deleteIfOrphaned)
			{
				mesh->updateEdgePolygonReferences(polygonIndex, oldEdgeIndex, newEdgeIndex, deleteIfOrphaned);
			}

		public:

			// Default constructor
			Mesh();

			explicit Mesh(Logger* logger);

			Mesh(
				UserAttributesFactory* vertexFactory, 
				UserAttributesFactory* edgeFactory, 
				UserAttributesFactory* polygonFactory,
				UserAttributesFactory* polygonVertexFactory);

			Mesh(
				Logger* logger, 
				UserAttributesFactory* vertexFactory, 
				UserAttributesFactory* edgeFactory, 
				UserAttributesFactory* polygonFactory,
				UserAttributesFactory* polygonVertexFactory);

			// Copy constructor
			Mesh(Mesh const& other);

			// Destructor
			virtual ~Mesh();

			// Assignment operator
			Mesh& operator=(Mesh const& other);

			// User data
			void setRenderCallback(RenderCallback callback);

			void renderCallback(void* data);

			void clear();

			//
			// Vertices
			//

			// Add vertex, returning its index.
			uint32_t addVertex(Vertex vertex, int32_t prototype = -1);

			// Remove vertex
			void removeVertex(uint32_t vertexIndex, RemoveVertexResult* result = nullptr);

			void removeVertices(IndexVector const& vertexIndices);

			// Move the specified vertex by the given amount. No checking is done as to
			// whether this creates an invalid/'crossed-over' mesh.
			void moveVertex(uint32_t vertexIndex, Vector2 const& move);

			void moveVertexTo(uint32_t vertexIndex, Vector2 const& pos);

			// Move the specified vertices by the given amount. No checking is done as to
			// whether this creates an invalid/'crossed-over' mesh.
			void moveVertices(IndexVector const& vertexIndices, Vector2 const& move);

			// User data
			void setVertexUserData(uint32_t vertexIndex, void const* data);

			void const* getVertexUserData(uint32_t vertexIndex) const;

			void getVertexUvAttribute(uint32_t vertexIndex, uint32_t textureIndex, float& u, float& v) const;

			void getVertexRgbaAttribute(uint32_t vertexIndex, float& r, float& g, float& b, float& a) const;

			// Get the number of alive vertices.
			uint32_t getNumVertices() const;

			// Get first live vertex index.
			uint32_t getFirstVertexIndex() const;

			// Get the vertex index following the given one.
			uint32_t getNextVertexIndex(uint32_t vertexIndex) const;

			// Query whether the given vertex is past the last one.
			bool vertexIndexIterationFinished(uint32_t vertexIndex) const;

			// Get const reference to the vertex.  All public access is non-mutable.
			Vertex const& getVertex(uint32_t vertexIndex) const;

			//
			// Edges
			//

			// Add edge, returning its index.
			uint32_t addEdge(Edge edge, int32_t prototype = -1);

			// Move the specified edge by the given amount. No checking is done as to
			// whether this creates an invalid/'crossed-over' mesh.
			void moveEdge(uint32_t edgeIndex, Vector2 const& move);

			// Move the specified edges by the given amount. No checking is done as to
			// whether this creates an invalid/'crossed-over' mesh.
			void moveEdges(IndexVector const& edgeIndices, Vector2 const& move);

			// Move the edge vertices along the edge so that the edge is the specified length
			void setEdgeLength(uint32_t edgeIndex, float length);

			// User data
			void setEdgeUserData(uint32_t edgeIndex, void const* data);

			void const* getEdgeUserData(uint32_t edgeIndex) const;

			void getEdgeRgbaAttribute(uint32_t edgeIndex, float& r, float& g, float& b, float& a) const;

			// Get the number of alive edges.
			uint32_t getNumEdges() const;

			// Get first live edge index.
			uint32_t getFirstEdgeIndex() const;

			// Get the edge index following the given one.
			uint32_t getNextEdgeIndex(uint32_t edgeIndex) const;

			// Query whether the given edge is past the last one.
			bool edgeIndexIterationFinished(uint32_t edgeIndex) const;

			// Get const reference to the edge.  All public access is non-mutable.
			Edge const& getEdge(uint32_t edgeIndex) const;

			int32_t getEdgeIndexByVertices(int32_t firstVertex, int32_t secondVertex, bool* correctOrder = nullptr) const;

			int32_t getEdgeIndexByOrderedVertices(int32_t firstVertex, int32_t secondVertex) const;

			// Get a list of edge indices by the given connectivity type.
			IndexVector getEdgeIndicesByConnectivity(Edge::Connectivity type) const;

			//
			// Polygons
			//

			// Add polygon, returning its index.
			uint32_t addPolygon(Polygon polygon, int32_t prototype = -1);

			void addHoleToPolygon(uint32_t polygonIndex, uint32_t holeIndex);

			void addFilledHoleToPolygon(uint32_t polygonIndex, uint32_t holeIndex, uint32_t* newFilledIndex = nullptr);

			void removeHoleFromPolygon(uint32_t polygonIndex, uint32_t holeIndex);

			void removeHolesFromPolygon(uint32_t polygonIndex);

			void removePolygon(uint32_t polygonIndex, bool deleteHoles = true);

			void removePolygons(IndexSet const& polygonIndices);

			// Move the specified polygon by the given amount. No checking is done as to
			// whether this creates an invalid/'crossed-over' mesh.
			void movePolygon(uint32_t polygonIndex, Vector2 const& move);

			// Move the specified polygons by the given amount. No checking is done as to
			// whether this creates an invalid/'crossed-over' mesh.
			void movePolygons(IndexVector const& polygonIndices, Vector2 const& move);

			// Move polygon so that the centre is in the new location.
			void recentrePolygon(uint32_t polygonIndex, Vector2 const& centre);

			// User data
			void setPolygonUserData(uint32_t polygonIndex, void const* data);

			void const* getPolygonUserData(uint32_t polygonIndex) const;

			void getPolygonRgbaAttribute(uint32_t polygonIndex, float& r, float& g, float& b, float& a) const;

			void getPolygonMaterialAttribute(uint32_t polygonIndex, std::string& material) const;

			void getPolygonProgramAttribute(uint32_t polygonIndex, std::string& program) const;

			void getPolygonTexturesAttribute(uint32_t polygonIndex, std::vector<std::string>& textures) const;

			void getPolygonColourType(uint32_t polygonIndex, UserAttributePolygonColourType& type) const;
				
			void setPolygonVertexUserData(uint32_t polygonIndex, uint32_t vertexIndex, void const* data);

			void const* getPolygonVertexUserData(uint32_t polygonIndex, uint32_t vertexIndex) const;

			void getPolygonVertexUvAttribute(uint32_t polygonIndex, uint32_t vertexIndex, uint32_t textureIndex, float& u, float& v) const;

			void getPolygonVertexUvWeightAttribute(uint32_t polygonIndex, uint32_t vertexIndex, uint32_t textureIndex, float& weight) const;

			void getPolygonVertexRgbaAttribute(uint32_t polygonIndex, uint32_t vertexIndex, float& r, float& g, float& b, float& a) const;

			// Get the number of alive polygons.
			uint32_t getNumPolygons() const;

			// Get first live polygon index.
			uint32_t getFirstPolygonIndex() const;

			// Get the polygon index following the given one.
			uint32_t getNextPolygonIndex(uint32_t polygonIndex) const;

			// Query whether the given polygon is past the last one.
			bool polygonIndexIterationFinished(uint32_t polygonIndex) const;

			// Get const reference to the polygon.  All public access is non-mutable.
			Polygon const& getPolygon(uint32_t polygonIndex) const;

			//
			// Utility
			//
			uint32_t addMeshCallbacks(MeshCallbacks const& callbacks);

			void removeMeshCallbacks(uint32_t id);

			void getExtents(Vector2& minExtent, Vector2& maxExtent) const;

			// Create acceleration grids, to speed up static queries.
			void createAccelerationGrids(float x, float y, float sizeX, float sizeY, int dimX, int dimY);

			AccelerationGrid const* _getVertexAccelerationGrid() const;

			AccelerationGrid const* _getEdgeAccelerationGrid() const;

			AccelerationGrid const* _getPolygonAccelerationGrid() const;

			//
			// High level
			//
			void merge(Mesh const* other);

			//
			// Query
			//
			int32_t getContainingPolygon(Vector2 const& position) const;

			bool pointInside(Vector2 const& position) const;

			bool lineIntersects(Vector2 const& l0, Vector2 const& l1) const;

			// Clean up dead objects, and reindex everything.
			void compact(std::map<uint32_t, uint32_t>* vertexRemapping = nullptr, std::map<uint32_t, uint32_t>* edgeRemapping = nullptr, std::map<uint32_t, uint32_t>* polygonRemapping = nullptr);

			MathsUtils::LineIntersectionType edgeEdgeIntersection(uint32_t edge0, uint32_t edge1, Vector2* point) const;

			IndexSet getVertexIndicesInBoundingBox(BoundingBox const& box) const;

			IndexSet getVertexIndicesInBoundingCircle(BoundingCircle const& circle) const;

			//
			// Debug
			//
			void print(std::ostream& out);

			void setIntegrityCheck(bool enable);

			void popIntegrityCheck();

			bool integrityCheckEnabled() const;
		};

	} // geometry
} // WP_NAMESPACE
