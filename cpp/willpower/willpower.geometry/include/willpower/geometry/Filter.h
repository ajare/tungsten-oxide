#pragma once

#include "willpower/common/Vector2.h"

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Types.h"
#include "willpower/geometry/Mesh.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class WP_GEOMETRY_API Filter
		{
		public:

			typedef std::function<bool(uint32_t)> FilterFunction;

			typedef std::function<bool(uint32_t, uint32_t)> CompareFunction;

		private:

			IndexSet mIndices;

		protected:

			Mesh const* mwMesh;

		protected:

			void setIndices(IndexSet const& indices);

			void addIndices(IndexSet const& indices);

			void removeIndices(IndexSet const& indices);

			void filter(FilterFunction func);

			void minElement(CompareFunction func);

			void maxElement(CompareFunction func);

		public:

			explicit Filter(Mesh const* mesh);

			IndexSet const& getIndices() const;
		};

	} // geometry
} // WP_NAMESPACE
