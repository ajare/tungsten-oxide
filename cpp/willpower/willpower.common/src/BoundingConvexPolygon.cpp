#include <algorithm>

#include "willpower/common/MathsUtils.h"
#include "willpower/common/BoundingConvexPolygon.h"

namespace WP_NAMESPACE
{

	using namespace std;
	
	BoundingConvexPolygon::BoundingConvexPolygon()
	{
	}

	BoundingConvexPolygon::BoundingConvexPolygon(Vector2 const& position, vector<Vector2> vertices)
		: mPosition(position)
		, mVertices(vertices)
	{
		updateExtents();
	}

	void BoundingConvexPolygon::updateExtents()
	{
		// Go through all points, and calculate
		mMinExtent.set(numeric_limits<float>::max(), numeric_limits<float>::max());
		mMaxExtent.set(numeric_limits<float>::lowest(), numeric_limits<float>::lowest());
		mCentre = Vector2::ZERO;

		for (auto const& vertex: mVertices)
		{
			if (vertex.x < mMinExtent.x)
			{
				mMinExtent.x = vertex.x;
			}
			if (vertex.y < mMinExtent.y)
			{
				mMinExtent.y = vertex.y;
			}
			if (vertex.x > mMaxExtent.x)
			{
				mMaxExtent.x = vertex.x;
			}
			if (vertex.y > mMaxExtent.y)
			{
				mMaxExtent.y = vertex.y;
			}

			mCentre += vertex;
		}

		if (!mVertices.empty())
		{
			mCentre /= (uint32_t)mVertices.size();
		}
	}

	void BoundingConvexPolygon::setPosition(Vector2 const& position)
	{
		setPosition(position.x, position.y);
	}

	void BoundingConvexPolygon::setPosition(float x, float y)
	{
		mPosition.set(x, y);
		updateExtents();

	}

	Vector2 const& BoundingConvexPolygon::getPosition() const
	{
		return mPosition;
	}

	void BoundingConvexPolygon::move(Vector2 const& distance)
	{
		move(distance.x, distance.y);
	}

	void BoundingConvexPolygon::move(float x, float y)
	{
		mPosition.x += x;
		mPosition.y += y;
		updateExtents();
	}


	Vector2 const& BoundingConvexPolygon::getCentre() const
	{
		return mCentre;
	}

	void BoundingConvexPolygon::getExtents(Vector2& minExtent, Vector2& maxExtent) const
	{
		minExtent = getMinExtent();
		maxExtent = getMaxExtent();
	}

	Vector2 const& BoundingConvexPolygon::getMinExtent() const
	{
		return mMinExtent;
	}

	Vector2 const& BoundingConvexPolygon::getMaxExtent() const
	{
		return mMaxExtent;
	}

	// Intersection tests
	bool BoundingConvexPolygon::pointInside(Vector2 const& point) const
	{
		return MathsUtils::pointInConvexPolygon(point, mVertices);
	}

	bool BoundingConvexPolygon::pointInside(float x, float y) const
	{
		return MathsUtils::pointInConvexPolygon(Vector2(x, y), mVertices);
	}

	bool BoundingConvexPolygon::intersectsTriMesh(Triangulation const& triangles) const
	{
		auto numTriangles = triangles.getNumTriangles();
		for (uint32_t i = 0; i < numTriangles; ++i)
		{
			Vector2 tv0, tv1, tv2;
			triangles.getTriangle(i, tv0, tv1, tv2);

			for (uint32_t j = 1; j < (uint32_t)mVertices.size() - 1; ++j)
			{
				Vector2 pv0 = mPosition + mVertices[0];
				Vector2 pv1 = mPosition + mVertices[j];
				Vector2 pv2 = mPosition + mVertices[j + 1];

				if (MathsUtils::triangleIntersectsTriangle(pv0, pv1, pv2, tv0, tv1, tv2))
				{
					return true;
				}
			}
		}

		return false;
	}

} // WP_NAMESPACE
