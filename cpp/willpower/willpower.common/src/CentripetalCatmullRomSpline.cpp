#include "willpower/common/CentripetalCatmullRomSpline.h"

using namespace std;

namespace WP_NAMESPACE
{

	CentripetalCatmullRomSpline::CentripetalCatmullRomSpline(vector<Vector2> const& points)
		: SplinePath(points)
		, mSpline(nullptr)
	{
		mSpline = new CubicHermiteSpline<Vector2>(mPoints, 0.5f);
		mLength = mSpline->totalLength();
		mMaxT = mSpline->getMaxT();
	}

	CentripetalCatmullRomSpline::~CentripetalCatmullRomSpline()
	{
		delete mSpline;
	}

	void CentripetalCatmullRomSpline::setControlPoint(int index, Vector2 const& position)
	{
		SplinePath::setControlPoint(index, position);

		delete mSpline;
		mSpline = new CubicHermiteSpline<Vector2>(mPoints, 0.5f);
		mLength = mSpline->totalLength();
		mMaxT = mSpline->getMaxT();
	}
	
	Vector2 CentripetalCatmullRomSpline::getPosition(float distance) const
	{
		return mSpline->getPosition((distance / mLength) * mMaxT);
	}

	Vector2 CentripetalCatmullRomSpline::getDirection(float distance) const
	{
		return mSpline->getTangent((distance / mLength) * mMaxT).tangent;
	}

	Vector2 CentripetalCatmullRomSpline::getAcceleration(float distance) const
	{
		return mSpline->getCurvature((distance / mLength) * mMaxT).curvature;
	}

	float CentripetalCatmullRomSpline::getLength() const
	{
		return mLength;
	}

} // WP_NAMESPACE
