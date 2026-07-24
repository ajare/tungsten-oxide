#pragma once

#include "willpower/common/BezierSpline.h"

#include "willpower/geometry/Offsetter.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class BezierOffsetter : public Offsetter
		{
			BezierSpline mCurve;

			float mScale;

		public:

			BezierOffsetter(std::vector<wp::Vector2> const& points, float maxMiter, float scale);

			void offset(float amount1, float amount2, CornerType cornerType, WidthModificationFunction widthModifier = defaultWidthModifier, int startVertex = 0, int endVertex = -1);
		};

	} // geometry
} // WP_NAMESPACE
#pragma once
