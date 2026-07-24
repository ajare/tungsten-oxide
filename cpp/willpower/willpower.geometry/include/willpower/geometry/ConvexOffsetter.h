#pragma once

#include "willpower/geometry/Offsetter.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class ConvexOffsetter : public Offsetter
		{
		public:

			ConvexOffsetter(std::vector<wp::Vector2> const& vertices, float maxMiter);

			void offset(float amount1, float amount2, CornerType cornerType, WidthModificationFunction widthModifier = defaultWidthModifier, int startVertex = 0, int endVertex = -1);
		};

	} // geometry
} // WP_NAMESPACE
