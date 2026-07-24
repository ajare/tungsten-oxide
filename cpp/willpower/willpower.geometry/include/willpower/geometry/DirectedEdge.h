#pragma once

#include <list>
#include <vector>


#include "willpower/geometry/Platform.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		struct DirectedEdgeVertex
		{
			uint32_t index{ ~0u };
			int32_t attributeIndex{ -1 };

			bool operator==(DirectedEdgeVertex const& other) const
			{
				return index == other.index && attributeIndex == other.attributeIndex;
			}

			bool operator!=(DirectedEdgeVertex const& other) const
			{
				return !(*this == other);
			}
		};

		struct DirectedEdge
		{
			uint32_t v0, v1;
			uint32_t index;

		public:

			bool operator==(DirectedEdge const& other) const
			{
				return v0 == other.v0 && v1 == other.v1 && index == other.index;
			}

			bool operator!=(DirectedEdge const& other) const
			{
				return !(*this == other);
			}
		};

		typedef std::list<DirectedEdge> DirectedEdgeList;
		typedef std::vector<DirectedEdge> DirectedEdgeVector;

		typedef DirectedEdgeList::const_iterator DirectedEdgeIterator;
		
	} // geometry
} // WP_NAMESPACE
