#include <algorithm>

#include "willpower/common/BezierSpline.h"

using namespace std;

namespace WP_NAMESPACE
{

	BezierSpline::BezierSpline()
		: SplinePath()
		, mRecursionLimit(8)
		, mPathEpsilon(1.0f)
		, mAngleToleranceEpsilon(0.01f)
		, mAngleTolerance(0.0f)
		, mCuspLimit(0.0f)
	{
	}

	BezierSpline::BezierSpline(vector<Vector2> const& points)
		: SplinePath(points)
		, mRecursionLimit(8)
		, mPathEpsilon(1.0f)
		, mAngleToleranceEpsilon(0.01f)
		, mAngleTolerance(0.0f)
		, mCuspLimit(0.0f)
	{
		if ((points.size() - 4) % 3 != 0)
		{
			throw exception("BezierSpline: incorrect number of points.");
		}
	}

	BezierSpline::BezierSpline(BezierSpline const& other)
	{
		copyFrom(other);
	}

	BezierSpline& BezierSpline::operator=(BezierSpline const& other)
	{
		copyFrom(other);
		return *this;
	}

	void BezierSpline::copyFrom(BezierSpline const& other)
	{
		mRecursionLimit = other.mRecursionLimit;
		mScale = other.mScale;
		mPathEpsilon = other.mPathEpsilon;
		mAngleToleranceEpsilon = other.mAngleToleranceEpsilon;
		mAngleTolerance = other.mAngleTolerance;
		mCuspLimit = other.mCuspLimit;
		mSegments = other.mSegments;
	}

	void BezierSpline::setControlPoint(int index, Vector2 const& position)
	{
		SplinePath::setControlPoint(index, position);
		mSegments.clear();
	}

	void BezierSpline::createSegments() const
	{
		// Create segments
		float offset = 0.0f;
		for (int i = 0; i < (int)mPoints.size() - 3; i += 3)
		{
			float length = calculateSegmentLength(i);

			Segment segment;
			segment.length = length;
			segment.offset = offset;

			mSegments.push_back(segment);

			offset += length;
		}
	}

	float BezierSpline::calculateSegmentLength(int point) const
	{
		float length = 0.0f;
		const int pieces = 100;

		Vector2 p0 = getPosition(point, 0.0f);
		for (int div = 1; div <= pieces; ++div)
		{
			Vector2 p1 = getPosition(point, div / (float)pieces);

			length += p0.distanceTo(p1);
			p0 = p1;
		}

		return length;
	}

	int BezierSpline::getSegmentIndex(float distance) const
	{
		if (mSegments.empty())
		{
			createSegments();
		}

		int curIndex = 0;

		auto it = mSegments.begin();
		float curDist = (*it).offset;
		while (true)
		{
			curDist += (*it).length;

			if (curDist >= distance)
				break;

			++it;
			++curIndex;
		}

		return curIndex;
	}

	BezierSpline::Segment const& BezierSpline::getSegment(int index) const
	{
		if (mSegments.empty())
		{
			createSegments();
		}

		return mSegments[index];
	}

	vector<Vector2> BezierSpline::divide(bool adaptive, float scale) const
	{
		if (mSegments.empty())
		{
			createSegments();
		}

		vector<Vector2> vertices;

		float tolerance = mPathEpsilon / scale;
		tolerance *= tolerance;

		vertices.push_back(mPoints.front());

		for (int i = 0; i < (int)mPoints.size() - 3; i += 3)
		{
			if (adaptive)
			{
				divideAdaptive(vertices, mPoints[i], mPoints[i + 1], mPoints[i + 2], mPoints[i + 3], tolerance, 0);
			}
			else
			{
				divideEqual(vertices, i);
			}
		}

		vertices.push_back(mPoints.back());
		return vertices;
	}

	void BezierSpline::divideAdaptive(vector<Vector2>& vertices, Vector2 const& v1, Vector2 const& v2, Vector2 const& v3, Vector2 const& v4, float tolerance, int depth) const
	{
		if (depth > mRecursionLimit)
		{
			return;
		}

		const float epsilon = 0.00001f;

		Vector2 v12 = (v1 + v2) / 2.0f;
		Vector2 v23 = (v2 + v3) / 2.0f;
		Vector2 v34 = (v3 + v4) / 2.0f;
		Vector2 v123 = (v12 + v23) / 2.0f;
		Vector2 v234 = (v23 + v34) / 2.0f;
		Vector2 v1234 = (v123 + v234) / 2.0f;

		if (depth > 0)
		{
			Vector2 d = v4 - v1;
			float d2 = fabs((v2.x - v4.x) * d.y - (v2.y - v4.y) * d.x);
			float d3 = fabs((v3.x - v4.x) * d.y - (v3.y - v4.y) * d.x);

			if (d2 > epsilon && d3 > epsilon)
			{
				if ((d2 + d3) * (d2 + d3) <= tolerance * d.dot(d))
				{
					if (mAngleTolerance < mAngleToleranceEpsilon)
					{
						vertices.push_back(v1234);
						return;
					}

					float a23 = atan2(v3.y - v2.y, v3.x - v2.x);
					float da1 = fabs(a23 - atan2(v2.y - v1.y, v2.x - v1.x));
					float da2 = fabs(a23 * atan2(v4.y - v3.y, v4.x - v3.x));

					if (da1 >= WP_PI)
					{
						da1 = WP_TWOPI - da1;
					}
					if (da2 >= WP_PI)
					{
						da2 = WP_TWOPI - da2;
					}

					if (da1 + da2 < mAngleTolerance)
					{
						vertices.push_back(v1234);
						return;
					}

					if (mCuspLimit != 0.0f)
					{
						if (da1 > mCuspLimit)
						{
							vertices.push_back(v2);
						}
						if (da2 > mCuspLimit)
						{
							vertices.push_back(v3);
						}
					}
				}
			}
			else
			{
				if (d2 > epsilon)
				{
					if (d2 * d2 <= tolerance * d.dot(d))
					{
						if (mAngleTolerance < mAngleToleranceEpsilon)
						{
							vertices.push_back(v1234);
							return;
						}

						float da1 = fabs(atan2(v3.y - v2.y, v3.x - v2.y) - atan2(v2.y - v1.y, v2.x - v1.x));
						if (da1 >= WP_PI)
						{
							da1 = WP_TWOPI - da1;
						}

						if (da1 < mAngleTolerance)
						{
							vertices.push_back(v2);
							vertices.push_back(v3);
							return;
						}

						if (mCuspLimit != 0.0f)
						{
							if (da1 > mCuspLimit)
							{
								vertices.push_back(v2);
								return;
							}
						}
					}
				}
				else if (d3 > epsilon)
				{
					if (d3 * d3 <= tolerance * d.dot(d))
					{
						if (mAngleTolerance < mAngleToleranceEpsilon)
						{
							vertices.push_back(v1234);
							return;
						}

						float da1 = fabs(atan2(v4.y - v3.y, v4.x - v3.y) - atan2(v3.y - v2.y, v3.x - v2.x));
						if (da1 >= WP_PI)
						{
							da1 = WP_TWOPI - da1;
						}

						if (da1 < mAngleTolerance)
						{
							vertices.push_back(v2);
							vertices.push_back(v3);
							return;
						}

						if (mCuspLimit != 0.0f)
						{
							if (da1 > mCuspLimit)
							{
								vertices.push_back(v3);
								return;
							}
						}
					}
				}
				else
				{
					d = v1234 - (v1 + v4) / 2.0f;
					if (d.dot(d) <= tolerance)
					{
						vertices.push_back(v1234);
						return;
					}
				}
			}
		}

		divideAdaptive(vertices, v1, v12, v123, v1234, tolerance, depth + 1);
		divideAdaptive(vertices, v1234, v234, v34, v4, tolerance, depth + 1);
	}

	void BezierSpline::divideEqual(vector<Vector2>& vertices, int segment) const
	{
		float dt = mScale / getSegment(segment / 3).length;

		float t = dt;
		while (t < 1.0f)
		{
			Vector2 pos = getPosition(segment, t);
			vertices.push_back(pos);

			t += dt;
		}
	}
	
	Vector2 BezierSpline::getPositionAtT(float t) const
	{
		float length = getLength();
		return getPosition(t * length);
	}

	Vector2 BezierSpline::getPosition(float distance) const
	{
		int seg = getSegmentIndex(distance);
		return getPosition(seg * 3, (distance - mSegments[seg].offset) / mSegments[seg].length);
	}

	Vector2 BezierSpline::getDirectionAtT(float t) const
	{
		float length = getLength();
		return getDirection(t * length);
	}

	Vector2 BezierSpline::getDirection(float distance) const
	{
		int seg = getSegmentIndex(distance);
		return get1stDerivative(seg * 3, (distance - mSegments[seg].offset) / mSegments[seg].length);
	}

	Vector2 BezierSpline::getAccelerationAtT(float t) const
	{
		float length = getLength();
		return getAcceleration(t * length);
	}

	Vector2 BezierSpline::getAcceleration(float distance) const
	{
		int seg = getSegmentIndex(distance);
		return get2ndDerivative(seg * 3, (distance - mSegments[seg].offset) / mSegments[seg].length);
	}

	float BezierSpline::getLength() const
	{
		if (mSegments.empty())
		{
			createSegments();
		}

		return mSegments.back().offset + mSegments.back().length;
	}

	Vector2 BezierSpline::getPosition(int point, float t) const
	{
		float t2 = t * t;
		float t3 = t2 * t;
		Vector2 a1 = mPoints[point + 0];
		Vector2 c1 = mPoints[point + 1];
		Vector2 c2 = mPoints[point + 2];
		Vector2 a2 = mPoints[point + 3];

		return (a2 + (c1 - c2) * 3 - a1) * t3 + (a1 - c1 * 2 + c2) * 3 * t2 + (c1 - a1) * 3 * t + a1;
	}

	Vector2 BezierSpline::get1stDerivative(int point, float t) const
	{
		float t2 = t * t;
		Vector2 a1 = mPoints[point + 0];
		Vector2 c1 = mPoints[point + 1];
		Vector2 c2 = mPoints[point + 2];
		Vector2 a2 = mPoints[point + 3];

		Vector2 dir = (a2 + (c1 - c2) * 3 - a1) * 3 * t2 + (a1 - c1 * 2 + c2) * 6 * t + (c1 - a1) * 3;
		return dir.normalisedCopy();
	}

	Vector2 BezierSpline::get2ndDerivative(int point, float t) const
	{
		Vector2 a1 = mPoints[point + 0];
		Vector2 c1 = mPoints[point + 1];
		Vector2 c2 = mPoints[point + 2];
		Vector2 a2 = mPoints[point + 3];

		Vector2 acc = (a2 + (c1 - c2) * 3 - a1) * 6 * t + (a1 - c1 * 2 + c2) * 6;
		return acc.normalisedCopy();
	}

	float BezierSpline::getCurvature(int point, float t) const
	{
		auto d = get1stDerivative(point, t);
		auto dd = get2ndDerivative(point, t);

		return fabs(d.x * dd.y - d.y * dd.x);
	}

	BoundingBox BezierSpline::getSegmentBounds(int segment) const
	{
		// Taken from Inigo Quilez:
		// http://www.iquilezles.org/www/articles/bezierbbox/bezierbbox.htm
		Vector2 p0 = mPoints[segment + 0];
		Vector2 p1 = mPoints[segment + 1];
		Vector2 p2 = mPoints[segment + 2];
		Vector2 p3 = mPoints[segment + 3];

		Vector2 mi(std::min(p0.x, p3.x), std::min(p0.y, p3.y));
		Vector2 ma(std::max(p0.x, p3.x), std::max(p0.y, p3.y));

		Vector2 a = 3 * (p1 - p2) + p3 - p0;
		Vector2 b = p0 + p2 - p1;
		Vector2 c = p1 - p0;
		Vector2 h = b * b - a * c;

		if (h.x > 0.0f)
		{
			h.x = sqrtf(h.x);

			float t = (-b.x - h.x) / a.x;
			if (t > 0.0f && t < 1.0f)
			{
				float s = 1.0f - t;
				float q = s * s * s* p0.x + 3.0f * s * s * t * p1.x + 3.0f * s * t * t * p2.x + t * t * t * p3.x;
				mi.x = min(mi.x, q);
				ma.x = max(ma.x, q);
			}

			t = (-b.x + h.x) / a.x;
			if (t > 0.0f && t < 1.0f)
			{
				float s = 1.0f - t;
				float q = s * s * s * p0.x + 3.0f * s * s * t * p1.x + 3.0f * s * t * t * p2.x + t * t * t * p3.x;
				mi.x = min(mi.x, q);
				ma.x = max(ma.x, q);
			}
		}

		if (h.y > 0.0)
		{
			h.y = sqrtf(h.y);

			float t = (-b.y - h.y) / a.y;
			if (t > 0.0f && t < 1.0f)
			{
				float s = 1.0f - t;
				float q = s * s * s * p0.y + 3.0f * s * s * t * p1.y + 3.0f * s * t * t * p2.y + t * t * t *p3.y;
				mi.y = min(mi.y, q);
				ma.y = max(ma.y, q);
			}
			t = (-b.y + h.y) / a.y;
			if (t > 0.0f && t < 1.0f)
			{
				float s = 1.0f - t;
				float q = s * s * s * p0.y + 3.0f * s * s * t * p1.y + 3.0f * s * t * t * p2.y + t * t * t  *p3.y;
				mi.y = min(mi.y, q);
				ma.y = max(ma.y, q);
			}
		}

		return BoundingBox(mi, ma - mi);
	}

	BoundingBox BezierSpline::getBounds() const
	{
		BoundingBox bounds = getSegmentBounds(0);

		for (int i = 3; i < (int)mPoints.size() - 3; i += 3)
		{
			BoundingBox segBounds = getSegmentBounds(i);
			bounds.unionWith(segBounds);
		}

		return bounds;
	}
} // WP_NAMESPACE
