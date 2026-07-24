#pragma once

#include <vector>
#include <array>

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Types.h"
#include "willpower/geometry/Offsetter.h"
#include "willpower/geometry/DirectedEdge.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		struct ExtrudeVertexResult
		{
			// Extruded vertex
			uint32_t vertexIndex;

			// Affected polygon
			uint32_t affectedPolygon;

			// New vertex indices
			IndexVector newVertexIndices;

			// New edge indices
			IndexVector newEdgeIndices;
		};

		struct ExtrudePolygonResult
		{
			struct Polygon
			{
				// New polygon
				uint32_t index;

				// Hole (ie old polygon, if unmerged full loop)
				uint32_t holeIndex;

				// New edge indices
				IndexVector extrudedEdgeIndices;

				// Old edges (ie non-merged)
				IndexVector sourceEdgeIndices;
			};

			// Polygons created
			std::vector<Polygon> polygons;
		};

		struct BridgeEdgesResult
		{
			struct Polygon
			{
				// New polygon
				uint32_t index;

				// Side edges
				IndexVector edges[2];
			};

			// Polygons created
			std::vector<Polygon> polygons;

			uint32_t polygonRemovedInMergeIndex;
		};

		struct WeldEdgesResult
		{
			uint32_t weldingEdges[2];

			uint32_t polygonIndices[2];
			
			// New edge
			uint32_t weldedEdge;
		};

		struct SplitEdgeResult
		{
			// Split edge
			uint32_t splitEdgeIndex;

			// Split edge vertices
			std::array<uint32_t, 2> splitEdgeVertexIndices;

			// Affected polygons
			std::vector<uint32_t> affectedPolygons;

			// New vertices
			std::vector<uint32_t> newVertexIndices;

			// New edges
			std::vector<uint32_t> newEdgeIndices;
		};

		struct SetEdgeLengthResult
		{
			// Edge
			uint32_t edgeIndex;

			// Vertices
			std::array<uint32_t, 2> splitEdgeVertexIndices;

			// Affected polygons
			std::vector<uint32_t> affectedPolygons;
		};

		struct MergePolygonsResult
		{
			// Old indices;
			uint32_t oldIndices[2];

			// New index
			uint32_t newIndex;

			// Edges removed
			IndexVector edgesRemoved;

			// Vertices removed
			IndexVector verticesRemoved;
		};

		struct SplitPolygonResult
		{
			// New polygon created
			uint32_t newPolygonIndex;

			// New edges which have split the polygon
			IndexVector splittingEdgeIndices;
		};

		struct SlicePolygonResult
		{
			// New polygon created
			int32_t newPolygonIndex;

			// New edges resulting from slice
			IndexVector slicingEdgeIndices;
		};

		struct CutPolygonResult
		{
			// New edge(s) which have cut the polygon
			IndexVector cuttingEdgeIndices;

			// Edges which have been removed
			DirectedEdgeVector edgesRemoved;

			// Holes which have been removed
			IndexVector holesRemovedIndices;
		};

		struct RemoveVertexResult
		{
			// Edge created.
			uint32_t newEdgeIndex;
		};

		struct SnipVertexResult
		{
			// Snipped vertex
			uint32_t vertexIndex;

			// Affected polygon
			uint32_t affectedPolygon;

			// Edge created, when not constraining the vertex.
			uint32_t newEdgeIndex;

			// Edge(s) created, when constraining
			uint32_t newConstrainedEdgeIndices[2];
		};

		struct ChamferVertexResult
		{
			// Chamfered vertex
			uint32_t vertexIndex;

			// Affected polygon
			uint32_t affectedPolygon;

			// Vertices created
			IndexVector newVertexIndices;

			// Edges created.
			IndexVector newEdgeIndices;
		};

	} // geometry
} // WP_NAMESPACE

