#include <algorithm>

#include "willpower/common/MathsUtils.h"
#include "willpower/common/BoundingBox.h"
#include "willpower/common/BoundingCircle.h"

using namespace std;

namespace WP_NAMESPACE
{

	BoundingBox::BoundingBox()
		: BoundingBox(Vector2::ZERO, Vector2::ZERO)
	{
	}

	BoundingBox::BoundingBox(BoundingBox const& other)
	{
		this->mPosition = other.mPosition;
		this->mSize = other.mSize;
		updateExtents();
	}

	BoundingBox::BoundingBox(Vector2 const& position, Vector2 const& size)
		: mPosition(position)
		, mSize(size)
		, mRadius(calculateRadius())
	{
		updateExtents();
	}

	BoundingBox::BoundingBox(float x, float y, float width, float height)
		: BoundingBox(Vector2(x, y), Vector2(width, height))
	{
	}

	BoundingBox::BoundingBox(Vector2 const& position, float size)
		: BoundingBox(position, Vector2(size, size))
	{
	}

	BoundingBox::BoundingBox(float x, float y, float size)
		: BoundingBox(Vector2(x, y), Vector2(size, size))
	{
	}

	BoundingBox::BoundingBox(vector<Vector2> const& points)
	{
		float minValue = numeric_limits<float>::lowest();
		float maxValue = numeric_limits<float>::max();
		Vector2 minExtent(maxValue, maxValue);
		Vector2 maxExtent(minValue, minValue);

		for (auto const& point: points)
		{
			if (point.x < minExtent.x)
				minExtent.x = point.x;
			if (point.x > maxExtent.x)
				maxExtent.x = point.x;
			if (point.y < minExtent.y)
				minExtent.y = point.y;
			if (point.y > maxExtent.y)
				maxExtent.y = point.y;
		}

		setPosition(minExtent);
		setSize(maxExtent - minExtent);
	}

	float BoundingBox::calculateRadius() const
	{
		return (mSize / 2.0f).length();
	}

	void BoundingBox::updateExtents()
	{
		mMinExtent.x = (std::min)(mPosition.x, mPosition.x + mSize.x);
		mMinExtent.y = (std::min)(mPosition.y, mPosition.y + mSize.y);
		mMaxExtent.x = (std::max)(mPosition.x, mPosition.x + mSize.x);
		mMaxExtent.y = (std::max)(mPosition.y, mPosition.y + mSize.y);
		mCentre = mPosition + mSize / 2.0f;
		mRadius = calculateRadius();
	}

	void BoundingBox::setPosition(Vector2 const& position)
	{
		setPosition(position.x, position.y);
	}

	void BoundingBox::setPosition(float x, float y)
	{
		mPosition.set(x, y);
		updateExtents();
	}

	void BoundingBox::centreOn(Vector2 const& position)
	{
		centreOn(position.x, position.y);
	}

	void BoundingBox::centreOn(float x, float y)
	{
		Vector2 halfSize = getHalfSize();

		mPosition.set(x - halfSize.x, y - halfSize.y);
		updateExtents();
	}

	Vector2 const& BoundingBox::getPosition() const
	{
		return mPosition;
	}

	void BoundingBox::move(Vector2 const& distance)
	{
		move(distance.x, distance.y);
	}

	void BoundingBox::move(float x, float y)
	{
		mPosition.x += x;
		mPosition.y += y;
	}

	void BoundingBox::setSize(Vector2 const& size)
	{
		setSize(size.x, size.y);
	}

	void BoundingBox::setSize(float width, float height)
	{
		mSize.set(width, height);
		updateExtents();
	}

	Vector2 const& BoundingBox::getSize() const
	{
		return mSize;
	}

	Vector2 BoundingBox::getHalfSize() const
	{
		return getSize() / 2;
	}

	void BoundingBox::setWidth(float width)
	{
		mSize.x = width;
		updateExtents();
	}

	void BoundingBox::setHeight(float height)
	{
		mSize.y = height;
		updateExtents();
	}

	float BoundingBox::getWidth() const
	{
		return mSize.x;
	}

	float BoundingBox::getHeight() const
	{
		return mSize.y;
	}

	Vector2 const& BoundingBox::getCentre() const
	{
		return mCentre;
	}

	void BoundingBox::getExtents(Vector2& minExtent, Vector2& maxExtent) const
	{
		minExtent = getMinExtent();
		maxExtent = getMaxExtent();
	}

	Vector2 const& BoundingBox::getMinExtent() const
	{
		return mMinExtent;
	}

	Vector2 const& BoundingBox::getMaxExtent() const
	{
		return mMaxExtent;
	}

	float BoundingBox::getRadius() const
	{
		return mRadius;
	}

	void BoundingBox::expand(float scale)
	{
		expand(mSize * scale);
	}

	void BoundingBox::expand(float x, float y)
	{
		expand(Vector2(x, y));
	}

	void BoundingBox::expand(Vector2 const& amt)
	{
		mPosition -= amt * 0.5f;
		mSize += amt;
		updateExtents();
	}

	void BoundingBox::expandToGrid(Vector2 const& gridSize)
	{
		float modX0, modY0, modX1, modY1;
		float cellX0, cellY0, cellX1, cellY1;

		modX0 = fmodf(mMinExtent.x, gridSize.x);
		modY0 = fmodf(mMinExtent.y, gridSize.y);
		modX1 = fmodf(mMaxExtent.x, gridSize.x);
		modY1 = fmodf(mMaxExtent.y, gridSize.y);

		cellX0 = (mMinExtent.x >= 0) ? mMinExtent.x - modX0 : mMinExtent.x - gridSize.x - modX0;
		cellY0 = (mMinExtent.y >= 0) ? mMinExtent.y - modY0 : mMinExtent.y - gridSize.y - modY0;
		cellX1 = (mMaxExtent.x >= 0) ? mMaxExtent.x + gridSize.x - modX1 : mMaxExtent.x - modX1;
		cellY1 = (mMaxExtent.y >= 0) ? mMaxExtent.y + gridSize.y - modY1 : mMaxExtent.y - modY1;

		setPosition(cellX0, cellY0);
		setSize(cellX1 - cellX0, cellY1 - cellY0);
	}

	void BoundingBox::inflate(float amount)
	{
		mPosition -= amount;
		mSize += amount * 2;
		updateExtents();
	}

	bool BoundingBox::BoundingBox::pointInside(Vector2 const& point) const
	{
		Vector2 minExtent, maxExtent;

		getExtents(minExtent, maxExtent);
		return MathsUtils::pointInBox(point, minExtent, maxExtent);
	}

	bool BoundingBox::pointInside(float x, float y) const
	{
		return pointInside(Vector2(x, y));
	}

	bool BoundingBox::contains(BoundingBox const* other) const
	{
		Vector2 thisMinExtent, thisMaxExtent, otherMinExtent, otherMaxExtent;

		getExtents(thisMinExtent, thisMaxExtent);
		other->getExtents(otherMinExtent, otherMaxExtent);

		return
			thisMinExtent.x <= otherMinExtent.x &&
			thisMinExtent.y <= otherMinExtent.y &&
			thisMaxExtent.x >= otherMaxExtent.x &&
			thisMaxExtent.y >= otherMaxExtent.y;
	}
	
	bool BoundingBox::intersectsBoundingObject(BoundingBox const* other) const
	{
		Vector2 thisMinExtent, thisMaxExtent, otherMinExtent, otherMaxExtent;

		getExtents(thisMinExtent, thisMaxExtent);
		other->getExtents(otherMinExtent, otherMaxExtent);
		return MathsUtils::boxIntersectsBox(thisMinExtent, thisMaxExtent, otherMinExtent, otherMaxExtent);
	}

	bool BoundingBox::intersectsBoundingObject(BoundingCircle const* other) const
	{
		Vector2 minExtent, maxExtent;
		getExtents(minExtent, maxExtent);

		return MathsUtils::boxIntersectsCircle(minExtent, maxExtent, other->getPosition(), other->getRadius());
	}

	bool BoundingBox::intersectsLine(Vector2 const& v0, Vector2 const& v1) const
	{
		Vector2 minExtent, maxExtent;
			
		getExtents(minExtent, maxExtent);
		return MathsUtils::lineIntersectsBox(v0, v1, minExtent, maxExtent) != MathsUtils::LineIntersectionType::NotIntersecting;
	}

	bool BoundingBox::intersectsLine(float v0x, float v0y, float v1x, float v1y) const
	{
		return intersectsLine(Vector2(v0x, v0y), Vector2(v1x, v1y));
	}

	bool BoundingBox::intersectsTriangle(Vector2 const& p0, Vector2 const& p1, Vector2 const& p2) const
	{
		Vector2 bv0(mMinExtent.x, mMinExtent.y);
		Vector2 bv1(mMaxExtent.x, mMinExtent.y);
		Vector2 bv2(mMaxExtent.x, mMaxExtent.y);
		Vector2 bv3(mMinExtent.x, mMaxExtent.y);

		if (MathsUtils::triangleIntersectsTriangle(bv0, bv1, bv2, p0, p1, p2))
		{
			return true;
		}
		
		return MathsUtils::triangleIntersectsTriangle(bv2, bv3, bv0, p0, p1, p2);
	}

	bool BoundingBox::intersectsTriMesh(Triangulation const& triangles) const
	{
		auto numTriangles = triangles.getNumTriangles();
		for (uint32_t i = 0; i < numTriangles; ++i)
		{
			Vector2 tv0, tv1, tv2;
			triangles.getTriangle(i, tv0, tv1, tv2);

			if (intersectsTriangle(tv0, tv1, tv2))
			{
				return true;
			}
		}

		return false;
	}

	BoundingBox BoundingBox::unionWith(BoundingBox const& other) const
	{
		Vector2 otherMinExtent, otherMaxExtent;

		other.getExtents(otherMinExtent, otherMaxExtent);

		float posX = (std::min)(mMinExtent.x, otherMinExtent.x);
		float posY = (std::min)(mMinExtent.y, otherMinExtent.y);
		float sizeX = (std::max)(mMaxExtent.x, otherMaxExtent.x) - posX;
		float sizeY = (std::max)(mMaxExtent.y, otherMaxExtent.y) - posY;

		return BoundingBox(posX, posY, sizeX, sizeY);
	}

} // WP_NAMESPACE
