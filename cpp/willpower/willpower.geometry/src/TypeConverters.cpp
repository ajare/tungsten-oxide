#include <algorithm>
#include <iterator>

#include "willpower/geometry/TypeConverters.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{
		using namespace std;

		DirectedEdgeList toDirectedEdgeList(DirectedEdgeVector const& edges)
		{
			DirectedEdgeList result;
			
			copy(edges.begin(), edges.end(), back_inserter(result));
			return result;
		}

		DirectedEdgeVector toDirectedEdgeVector(DirectedEdgeList const& edges)
		{
			DirectedEdgeVector result;

			copy(edges.begin(), edges.end(), back_inserter(result));
			return result;
		}

		IndexList toIndexList(DirectedEdgeVector const& edges)
		{
			IndexList result;
			
			for (auto const& edge: edges)
			{
				result.push_back(edge.index);
			}

			return result;
		}

		IndexList toIndexList(DirectedEdgeList const& edges)
		{
			IndexList result;

			for (auto const& edge: edges)
			{
				result.push_back(edge.index);
			}

			return result;
		}

		IndexVector toIndexVector(DirectedEdgeVector const& edges)
		{
			IndexVector result;

			for (auto const& edge: edges)
			{
				result.push_back(edge.index);
			}

			return result;
		}

		IndexVector toIndexVector(DirectedEdgeList const& edges)
		{
			IndexVector result;

			for (auto const& edge: edges)
			{
				result.push_back(edge.index);
			}

			return result;
		}

		IndexSet toIndexSet(DirectedEdgeVector const& edges)
		{
			IndexSet result;

			for (auto const& edge: edges)
			{
				result.insert(edge.index);
			}

			return result;
		}

		IndexSet toIndexSet(DirectedEdgeList const& edges)
		{
			IndexSet result;

			for (auto const& edge: edges)
			{
				result.insert(edge.index);
			}

			return result;
		}

	} // geometry
} //WP_NAMESPACE
