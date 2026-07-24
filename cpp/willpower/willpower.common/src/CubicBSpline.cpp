#include "willpower/common/CubicBSpline.h"

using namespace std;

namespace WP_NAMESPACE
{

	CubicBSpline::CubicBSpline(vector<Vector2> const& points)
		: SplinePath(points)
		, mSpline(nullptr)
	{
		mSpline = new UniformCubicBSpline<Vector2>(points);
		mLength = mSpline->totalLength();
		mMaxT = mSpline->getMaxT();
	}

	CubicBSpline::~CubicBSpline()
	{
		delete mSpline;
	}

	void CubicBSpline::setControlPoint(int index, Vector2 const& position)
	{
		SplinePath::setControlPoint(index, position);

		delete mSpline;
		mSpline = new UniformCubicBSpline<Vector2>(mPoints);
		mLength = mSpline->totalLength();
		mMaxT = mSpline->getMaxT();
	}

	Vector2 CubicBSpline::getPosition(float distance) const
	{
		return mSpline->getPosition((distance / mLength) * mMaxT);
	}

	Vector2 CubicBSpline::getDirection(float distance) const
	{
		return mSpline->getTangent((distance / mLength) * mMaxT).tangent;
	}

	Vector2 CubicBSpline::getAcceleration(float distance) const
	{
		return mSpline->getCurvature((distance / mLength) * mMaxT).curvature;
	}

	float CubicBSpline::getLength() const
	{
		return mLength;
	}

} // WP_NAMESPACE
