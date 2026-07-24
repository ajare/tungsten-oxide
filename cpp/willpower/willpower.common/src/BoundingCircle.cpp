#include <algorithm>

#include "willpower/common/MathsUtils.h"
#include "willpower/common/BoundingCircle.h"
#include "willpower/common/BoundingBox.h"

namespace WP_NAMESPACE
{

	BoundingCircle::BoundingCircle()
		: BoundingCircle(Vector2::ZERO, 0)
	{
	}

	BoundingCircle::BoundingCircle(BoundingCircle const& other)
	{
		this->mPosition = other.mPosition;
		this->mRadius = other.mRadius;
		updateExtents();
	}

	BoundingCircle::BoundingCircle(Vector2 const& position, float radius)
		: mPosition(position)
		, mRadius(radius)
	{
		updateExtents();
	}

	BoundingCircle::BoundingCircle(float x, float y, float radius)
		: BoundingCircle(Vector2(x, y), radius)
	{
	}

	void BoundingCircle::updateExtents()
	{
		mMinExtent.x = mPosition.x - mRadius;
		mMinExtent.y = mPosition.y - mRadius;
		mMaxExtent.x = mPosition.x + mRadius;
		mMaxExtent.y = mPosition.y + mRadius;
	}

	void BoundingCircle::setPosition(Vector2 const& position)
	{
		setPosition(position.x, position.y);
	}

	void BoundingCircle::setPosition(float x, float y)
	{
		mPosition.set(x, y);
		updateExtents();
	}

	Vector2 const& BoundingCircle::getPosition() const
	{
		return mPosition;
	}

	void BoundingCircle::move(Vector2 const& distance)
	{
		move(distance.x, distance.y);
	}

	void BoundingCircle::move(float x, float y)
	{
		mPosition.x += x;
		mPosition.y += y;
		updateExtents();
	}

	void BoundingCircle::setRadius(float radius)
	{
		mRadius = radius;
		updateExtents();
	}

	float BoundingCircle::getRadius() const
	{
		return mRadius;
	}

	void BoundingCircle::getExtents(Vector2& minExtent, Vector2& maxExtent) const
	{
		minExtent = getMinExtent();
		maxExtent = getMaxExtent();
	}

	Vector2 const& BoundingCircle::getMinExtent() const
	{
		return mMinExtent;
	}

	Vector2 const& BoundingCircle::getMaxExtent() const
	{
		return mMaxExtent;
	}

	bool BoundingCircle::pointInside(Vector2 const& point) const
	{
		return MathsUtils::pointInCircle(point, mPosition, mRadius);
	}

	bool BoundingCircle::pointInside(float x, float y) const
	{
		return pointInside(Vector2(x, y));
	}

	bool BoundingCircle::intersectsBoundingObject(BoundingCircle const* other) const
	{
		return MathsUtils::circleIntersectsCircle(mPosition, mRadius, other->getPosition(), other->getRadius());
	}

	bool BoundingCircle::intersectsBoundingObject(BoundingBox const* other) const
	{
		Vector2 otherMinExtent, otherMaxExtent;
		other->getExtents(otherMinExtent, otherMaxExtent);

		return MathsUtils::boxIntersectsCircle(otherMinExtent, otherMaxExtent, mPosition, mRadius);
	}

	bool BoundingCircle::intersectsLine(Vector2 const& v0, Vector2 const& v1) const
	{
		return MathsUtils::lineIntersectsCircle(v0, v1, mPosition, mRadius) != MathsUtils::LineIntersectionType::NotIntersecting;
	}

	bool BoundingCircle::intersectsLine(float v0x, float v0y, float v1x, float v1y) const
	{
		return intersectsLine(Vector2(v0x, v0y), Vector2(v1x, v1y));
	}

	bool BoundingCircle::intersectsTriMesh(Triangulation const& triangles) const
	{
		auto numTriangles = triangles.getNumTriangles();
		for (uint32_t i = 0; i < numTriangles; ++i)
		{
			Vector2 tv0, tv1, tv2;
			triangles.getTriangle(i, tv0, tv1, tv2);

			if (MathsUtils::circleIntersectsTriangle(mPosition, mRadius, tv0, tv1, tv2))
			{
				return true;
			}
		}

		return false;

	}

	BoundingCircle BoundingCircle::unionWith(BoundingCircle const& other)
	{
		Vector2 const& otherPosition = other.getPosition();
			
		Vector2 newCentre = mPosition.lerp(otherPosition, 0.5f);
		float newRadius = otherPosition.distanceTo(mPosition) / 2.0f;

		return BoundingCircle(newCentre, newRadius);
	}

} // WP_NAMESPACE
