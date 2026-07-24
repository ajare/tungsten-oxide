#pragma once

#include <vector>

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/MeshOperationResults.h"
#include "willpower/geometry/MeshOperationOptions.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{
		class Mesh;

		class WP_GEOMETRY_API MeshOperations
		{
		public:

			static ExtrudeVertexOptions DefaultExtrudeVertexOptions;

			static BridgeEdgesOptions DefaultBridgeOptions;

			static WeldEdgesOptions DefaultWeldEdgeOptions;

			static ExtrudePolygonOptions DefaultExtrudeOptions;

			static float OptimalBezierCurvature;

		private:

			static void snipCornerVertex(Mesh* mesh, uint32_t vertexIndex, uint32_t edgeIndex0, uint32_t edgeIndex1, float distance, SnipVertexResult* result = nullptr);

			static void extrudeVertexExternal(Mesh* mesh, uint32_t vertexIndex, float distance, ExtrudeVertexOptions options, ExtrudeVertexResult* result);

			static std::vector<DirectedEdgeVector> getExtrusionSections(Mesh* mesh, uint32_t polygonIndex, IndexVector const& edgeIndices, bool separate, bool allowLoop);

			static void doPolygonExtrusion(Mesh* mesh, uint32_t polygonIndex, std::vector<DirectedEdgeVector> const& edgeSections, bool directed, Vector2 const& extrusion, ExtrudePolygonOptions options, ExtrudePolygonResult* result);

			static void extrudePolygonEdgesDirected(Mesh* mesh, uint32_t polygonIndex, DirectedEdgeVector const& edgeData, Vector2 const& extrusion, ExtrudePolygonOptions options, uint32_t* endVertices, uint32_t* collinearVertices, uint32_t* newPolygonIndex, uint32_t* holeIndex, IndexVector* extrudedEdgeIndices, IndexVector* sourceEdgeIndices);

			static void extrudePolygonEdgesNormal(Mesh* mesh, uint32_t polygonIndex, DirectedEdgeVector const& edgeData, float distance, ExtrudePolygonOptions options, uint32_t* endVertices, uint32_t* collinearVertices, uint32_t* newPolygonIndex, uint32_t* holeIndex, IndexVector* extrudedEdgeIndices, IndexVector* sourceEdgeIndices);

			// The opposite of merge: split a polygon by creating a new edge
			static void splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t vertex2Index, SplitPolygonResult* result = nullptr);

			static void splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t vertex2Index, IndexVector const& vertexIndices, SplitPolygonResult* result = nullptr);

			static void splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t vertex2Index, SplitPolygonResult* result = nullptr);

			static void splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t vertex2Index, IndexVector const& vertexIndices, SplitPolygonResult* result = nullptr);

			static void splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t edge2Index, float edge2Amt, SplitPolygonResult* result = nullptr);

			static void splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t edge2Index, float edge2Amt, IndexVector const& vertexIndices, SplitPolygonResult* result = nullptr);

			static void splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t edge2Index, float edge2Amt, SplitPolygonResult* result = nullptr);

			static void splitPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t edge2Index, float edge2Amt, IndexVector const& vertexIndices, SplitPolygonResult* result = nullptr);

		public:

			static void extrudeVertex(Mesh* mesh, uint32_t vertexIndex, float distance, ExtrudeVertexOptions options = DefaultExtrudeVertexOptions, ExtrudeVertexResult* result = nullptr);

			static void chamferVertex(Mesh* mesh, uint32_t vertexIndex, float distance, float tightness = OptimalBezierCurvature, ChamferVertexResult* result = nullptr);

			static void snipVertex(Mesh* mesh, uint32_t vertexIndex, float distance, SnipVertexResult* result = nullptr);

			// Split an edge at normalised distance 't', returning the index of the created vertex by pointer.
			static void splitEdge(Mesh* mesh, uint32_t edgeIndex, float t, SplitEdgeResult* result = nullptr);

			static void splitEdge(Mesh* mesh, uint32_t edgeIndex, int edgeCount, SplitEdgeResult* result = nullptr);

			// Set an edge's length, eg after splitting
			static void setEdgeLength(Mesh* mesh, uint32_t edgeIndex, float length, SetEdgeLengthResult* result = nullptr);

			// Bridge two sets of edges by creating a polygon between them
			static void bridgeEdges(Mesh* mesh, uint32_t sourceEdgeIndex, uint32_t targetEdgeIndex, BridgeEdgesOptions options = DefaultBridgeOptions, BridgeEdgesResult* result = nullptr);

			static void bridgeEdges(Mesh* mesh, IndexVector const& sourceEdgeIndices, IndexVector const& targetEdgeIndices, BridgeEdgesOptions options = DefaultBridgeOptions, BridgeEdgesResult* result = nullptr);

			static void weldEdges(Mesh* mesh, uint32_t edge0Index, uint32_t edge1Index, WeldEdgesOptions options = DefaultWeldEdgeOptions, WeldEdgesResult* result = nullptr);

			// Extrude polygon edges along their average normal
			static void extrudePolygonNormal(Mesh* mesh, uint32_t polygonIndex, IndexVector const& edgeIndices, float distance, ExtrudePolygonOptions options = DefaultExtrudeOptions, ExtrudePolygonResult* result = nullptr);

			// Extrude polygon edges in a given direction
			static void extrudePolygonDirected(Mesh* mesh, uint32_t polygonIndex, IndexVector const& edgeIndices, Vector2 const& extrusion, ExtrudePolygonOptions options = DefaultExtrudeOptions, ExtrudePolygonResult* result = nullptr);

			// Cut a polygon with an arbitrary line
			static void slicePolygon(Mesh* mesh, uint32_t polygonIndex, Vector2 const& v0, Vector2 const& v1, bool removeSliced, SlicePolygonResult* result = nullptr);

			// Cut a polygon through two of its vertices
			static void slicePolygon(Mesh* mesh, uint32_t polygonIndex, uint32_t vertex1Index, uint32_t vertex2Index, bool removeSliced, SlicePolygonResult* result = nullptr);

			// remove part of a polygon
			static void cutPolygon(Mesh* mesh, uint32_t polyIndex, Vector2 const& v0, Vector2 const& v1, CutPolygonResult* result = nullptr);

			static void cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t vertex2Index, CutPolygonResult* result = nullptr);

			static void cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t vertex2Index, IndexVector const& vertexIndices, CutPolygonResult* result = nullptr);

			static void cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t vertex2Index, CutPolygonResult* result = nullptr);

			static void cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t vertex2Index, IndexVector const& vertexIndices, CutPolygonResult* result = nullptr);

			static void cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t edge2Index, float edge2Amt, CutPolygonResult* result = nullptr);

			static void cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t vertex1Index, uint32_t edge2Index, float edge2Amt, IndexVector const& vertexIndices, CutPolygonResult* result = nullptr);

			static void cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t edge2Index, float edge2Amt, CutPolygonResult* result = nullptr);

			static void cutPolygon(Mesh* mesh, uint32_t polyIndex, uint32_t edge1Index, float edge1Amt, uint32_t edge2Index, float edge2Amt, IndexVector const& vertexIndices, CutPolygonResult* result = nullptr);

			// Merge two polygons which share edges.
			static void mergePolygonsByEdge(Mesh* mesh, uint32_t poly1Index, uint32_t poly2Index, float vertexTolerance = -1.0f, MergePolygonsResult* result = nullptr);
		};

	} // geometry
} // WP_NAMESPACE

