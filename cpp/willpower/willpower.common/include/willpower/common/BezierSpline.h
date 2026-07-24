#pragma once

#include <vector>

#include "willpower/common/Platform.h"
#include "willpower/common/Vector2.h"
#include "willpower/common/SplinePath.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API BezierSpline : public SplinePath
	{
		struct Segment
		{
			float offset, length;
		};

	private:

		int mRecursionLimit;

		float mScale, mPathEpsilon, mAngleToleranceEpsilon, mAngleTolerance, mCuspLimit;

		mutable std::vector<Segment> mSegments;

	private:

		void copyFrom(BezierSpline const& other);

		void createSegments() const;

		void divideAdaptive(std::vector<Vector2>& vertices, Vector2 const& v1, Vector2 const& v2, Vector2 const& v3, Vector2 const& v4, float tolerance, int depth) const;

		void divideEqual(std::vector<Vector2>& vertices, int segment) const;

		int getSegmentIndex(float distance) const;

		Segment const& getSegment(int index) const;

		float calculateSegmentLength(int point) const;

		BoundingBox getSegmentBounds(int segment) const;

	protected:

		Vector2 getPosition(int point, float t) const;

		Vector2 get1stDerivative(int point, float t) const;

		Vector2 get2ndDerivative(int point, float t) const;

		float getCurvature(int point, float t) const;

	public:

		BezierSpline();

		explicit BezierSpline(std::vector<wp::Vector2> const& points);

		BezierSpline(BezierSpline const& other);

		BezierSpline& operator=(BezierSpline const& other);

		void setControlPoint(int index, Vector2 const& position);

		std::vector<Vector2> divide(bool adaptive, float scale = 1.0f) const;

		Vector2 getPositionAtT(float t) const;

		Vector2 getPosition(float distance) const;

		Vector2 getDirectionAtT(float distance) const;

		Vector2 getDirection(float distance) const;

		Vector2 getAccelerationAtT(float distance) const;

		Vector2 getAcceleration(float distance) const;

		float getLength() const;

		BoundingBox getBounds() const;
	};

} // WP_NAMESPACE

