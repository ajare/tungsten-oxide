#include "willpower/common/CubicBSplineLooping.h"

using namespace std;

namespace WP_NAMESPACE
{

	CubicBSplineLooping::CubicBSplineLooping(vector<Vector2> const& points)
		: SplinePath(points)
		, mSpline(nullptr)
	{
		mSpline = new LoopingUniformCubicBSpline<Vector2>(points);
		mLength = mSpline->totalLength();
		mMaxT = mSpline->getMaxT();
	}

	CubicBSplineLooping::~CubicBSplineLooping()
	{
		delete mSpline;
	}

	void CubicBSplineLooping::setControlPoint(int index, Vector2 const& position)
	{
		SplinePath::setControlPoint(index, position);

		delete mSpline;
		mSpline = new LoopingUniformCubicBSpline<Vector2>(mPoints);
		mLength = mSpline->totalLength();
		mMaxT = mSpline->getMaxT();
	}

	Vector2 CubicBSplineLooping::getPosition(float distance) const
	{
		return mSpline->getPosition((distance / mLength) * mMaxT);
	}

	Vector2 CubicBSplineLooping::getDirection(float distance) const
	{
		return mSpline->getTangent((distance / mLength) * mMaxT).tangent;
	}

	Vector2 CubicBSplineLooping::getAcceleration(float distance) const
	{
		return mSpline->getCurvature((distance / mLength) * mMaxT).curvature;
	}

	float CubicBSplineLooping::getLength() const
	{
		return mLength;
	}

} // WP_NAMESPACE
