#pragma once

#include "willpower/geometry/Types.h"
#include "willpower/geometry/DirectedEdge.h"
#include "willpower/geometry/Platform.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		DirectedEdgeList toDirectedEdgeList(DirectedEdgeVector const& edges);

		DirectedEdgeVector toDirectedEdgeVector(DirectedEdgeList const& edges);

		IndexList toIndexList(DirectedEdgeVector const& edges);

		IndexList toIndexList(DirectedEdgeList const& edges);

		IndexVector toIndexVector(DirectedEdgeVector const& edges);

		IndexVector toIndexVector(DirectedEdgeList const& edges);

		IndexSet toIndexSet(DirectedEdgeVector const& edges);

		IndexSet toIndexSet(DirectedEdgeList const& edges);

	} // geometry
} // WP_NAMESPACE
#pragma once
