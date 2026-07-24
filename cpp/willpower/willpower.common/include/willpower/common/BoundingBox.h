#pragma once

#include "willpower/common/Platform.h"
#include "willpower/common/Vector2.h"
#include "willpower/common/Triangulation.h"

namespace WP_NAMESPACE
{

	class BoundingCircle;

	class WP_COMMON_API BoundingBox
	{
		Vector2 mPosition;

		Vector2 mSize;

		Vector2 mMinExtent, mMaxExtent, mCentre;

		float mRadius;

	private:

		float calculateRadius() const;

		void updateExtents();

	public:

		BoundingBox();

		BoundingBox(BoundingBox const& other);

		BoundingBox(Vector2 const& position, Vector2 const& size);

		BoundingBox(float x, float y, float width, float height);

		BoundingBox(Vector2 const& position, float size);

		BoundingBox(float x, float y, float size);

		BoundingBox(std::vector<Vector2> const& points);

		void setPosition(Vector2 const& position);

		void setPosition(float x, float y);

		void centreOn(Vector2 const& position);

		void centreOn(float x, float y);

		Vector2 const& getPosition() const;

		void move(Vector2 const& distance);

		void move(float x, float y);

		void setSize(Vector2 const& size);

		void setSize(float width, float height);

		Vector2 const& getSize() const;

		Vector2 getHalfSize() const;

		void setWidth(float width);

		void setHeight(float height);

		float getWidth() const;

		float getHeight() const;

		Vector2 const& getCentre() const;

		void getExtents(Vector2& minExtent, Vector2& maxExtent) const;

		Vector2 const& getMinExtent() const;

		Vector2 const& getMaxExtent() const;

		float getRadius() const;

		void expand(float scale);

		void expand(float x, float y);

		void expand(Vector2 const& amt);

		void expandToGrid(Vector2 const& gridSize);

		void inflate(float amount);

		// Intersection tests
		bool pointInside(Vector2 const& point) const;

		bool pointInside(float x, float y) const;

		bool contains(BoundingBox const* other) const;

		bool intersectsBoundingObject(BoundingBox const* other) const;

		bool intersectsBoundingObject(BoundingCircle const* other) const;

		bool intersectsLine(Vector2 const& v0, Vector2 const& v1) const;

		bool intersectsLine(float v0x, float v0y, float v1x, float v1y) const;

		bool intersectsTriangle(Vector2 const& p0, Vector2 const& p1, Vector2 const& p2) const;

		bool intersectsTriMesh(Triangulation const& triangles) const;

		// Utility
		BoundingBox unionWith(BoundingBox const& other) const;
	};

} // WP_NAMESPACE
