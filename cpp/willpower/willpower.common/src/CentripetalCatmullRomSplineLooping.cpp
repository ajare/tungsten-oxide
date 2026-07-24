#include "willpower/common/CentripetalCatmullRomSplineLooping.h"

using namespace std;

namespace WP_NAMESPACE
{

	CentripetalCatmullRomSplineLooping::CentripetalCatmullRomSplineLooping(vector<Vector2> const& points)
		: SplinePath(points)
		, mSpline(nullptr)
	{
		mSpline = new LoopingCubicHermiteSpline<Vector2>(mPoints, 0.5f);
		mLength = mSpline->totalLength();
		mMaxT = mSpline->getMaxT();
	}

	CentripetalCatmullRomSplineLooping::~CentripetalCatmullRomSplineLooping()
	{
		delete mSpline;
	}

	void CentripetalCatmullRomSplineLooping::setControlPoint(int index, Vector2 const& position)
	{
		SplinePath::setControlPoint(index, position);
		
		delete mSpline;
		mSpline = new LoopingCubicHermiteSpline<Vector2>(mPoints, 0.5f);
		mLength = mSpline->totalLength();
		mMaxT = mSpline->getMaxT();
	}

	Vector2 CentripetalCatmullRomSplineLooping::getPosition(float distance) const
	{
		return mSpline->getPosition((distance / mLength) * mMaxT);
	}

	Vector2 CentripetalCatmullRomSplineLooping::getDirection(float distance) const
	{
		return mSpline->getTangent((distance / mLength) * mMaxT).tangent.normalisedCopy();
	}

	Vector2 CentripetalCatmullRomSplineLooping::getAcceleration(float distance) const
	{
		return mSpline->getCurvature((distance / mLength) * mMaxT).curvature;
	}

	float CentripetalCatmullRomSplineLooping::getLength() const
	{
		return mLength;
	}

} // WP_NAMESPACE
