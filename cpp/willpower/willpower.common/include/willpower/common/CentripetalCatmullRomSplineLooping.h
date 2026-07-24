#pragma once

#include <vector>

#pragma warning(push)
#pragma warning(disable: 4244)
#include <spline_library/splines/cubic_hermite_spline.h>
#pragma warning(pop)

#include "willpower/common/Platform.h"
#include "willpower/common/Vector2.h"
#include "willpower/common/SplinePath.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API CentripetalCatmullRomSplineLooping : public SplinePath
	{
		LoopingCubicHermiteSpline<Vector2>* mSpline;

		float mLength, mMaxT;

	public:

		explicit CentripetalCatmullRomSplineLooping(std::vector<wp::Vector2> const& points);

		~CentripetalCatmullRomSplineLooping();

		void setControlPoint(int index, Vector2 const& position);

		Vector2 getPosition(float distance) const;

		Vector2 getDirection(float distance) const;

		Vector2 getAcceleration(float distance) const;

		float getLength() const;
	};

} // WP_NAMESPACE

