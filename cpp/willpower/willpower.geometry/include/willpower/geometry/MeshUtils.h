#pragma once

#include <vector>
#include <list>
#include <set>
#include <tuple>

#include "willpower/common/Vector2.h"
#include "willpower/common/Globals.h"

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Types.h"
#include "willpower/geometry/DirectedEdge.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{
		class Mesh;

		class WP_GEOMETRY_API MeshUtils
		{
		public:

			typedef std::tuple<uint32_t, uint32_t, uint32_t> EdgeIndexInfo;

			typedef std::vector<EdgeIndexInfo> EdgeInfoVector;

			typedef std::list<EdgeIndexInfo> EdgeInfoList;

			typedef std::vector<EdgeInfoVector> OrderedEdgeGroupSet;

		public:

			static OrderedEdgeGroupSet groupConnectedEdges(std::list<EdgeIndexInfo> const& edgeList, bool allowFlips = false);

			static OrderedEdgeGroupSet groupConnectedEdges(Mesh const* mesh, uint32_t polygonIndex, IndexVector const& edgeIndices, bool allowFlips = false);

			static std::vector<IndexVector> groupConnectedPolygons(Mesh const* mesh);

			static uint32_t getNearestEdgeIndex(Mesh const* mesh, Vector2 const& point, IndexSet const& edgeIndices);

			static float anticlockwiseAngleBetweenConnectedEdges(Mesh const* mesh, uint32_t edge0Index, uint32_t edge1Index);

			static float clockwiseAngleBetweenConnectedEdges(Mesh const* mesh, uint32_t edge0Index, uint32_t edge1Index);

			static Winding getVertexWinding(std::vector<Vector2> const& vertices);

			static std::vector<std::vector<Vector2>> insetVertexLoop(std::vector<Vector2> const& vertices, float distance, bool rounded);

			static std::vector<std::vector<Vector2>> insetVertexLoops(std::vector<std::vector<Vector2>> const& loops, float distance, bool rounded);

			static EdgeInfoList getEdgeInfo(IndexVector const& edgeIndices, Mesh* mesh);

			static std::list<DirectedEdgeVector> splitDirectedEdgeVector(DirectedEdgeVector const& edges, IndexVector const& delims);

			static IndexSet getPolygonReferences(Mesh const* mesh, IndexVector const& edgeIndices);

			static Vector2 calculateEdgeListCentre(Mesh const* mesh, EdgeInfoVector const& edgeInfo, uint32_t* edgeIndexResult = nullptr);

			static bool areEdgesInPolygon(Mesh const* mesh, uint32_t polygonIndex, IndexVector const& edgeIndices);

			static std::vector<std::vector<wp::Vector2>> unionPolygons(std::vector<std::vector<wp::Vector2>> const& polygons);
		};

	} // geometry
} // WP_NAMESPACE
