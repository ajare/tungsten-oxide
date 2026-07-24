#pragma once

#include "willpower/common/Platform.h"
#include "willpower/common/Vector2.h"
#include "willpower/common/Triangulation.h"

namespace WP_NAMESPACE
{

	class BoundingBox;

	class WP_COMMON_API BoundingCircle
	{
		Vector2 mPosition;
			
		float mRadius;

		Vector2 mMinExtent, mMaxExtent;

	private:

		void updateExtents();

	public:

		BoundingCircle();

		BoundingCircle(BoundingCircle const& other);

		BoundingCircle(Vector2 const& position, float radius);

		BoundingCircle(float x, float y, float radius);

		void setPosition(Vector2 const& position);

		void setPosition(float x, float y);

		Vector2 const& getPosition() const;

		void move(Vector2 const& distance);

		void move(float x, float y);

		void setRadius(float radius);
			
		float getRadius() const;

		void getExtents(Vector2& minExtent, Vector2& maxExtent) const;

		Vector2 const& getMinExtent() const;

		Vector2 const& getMaxExtent() const;

		// Intersection tests
		bool pointInside(Vector2 const& point) const;

		bool pointInside(float x, float y) const;

		bool intersectsBoundingObject(BoundingCircle const* other) const;

		bool intersectsBoundingObject(BoundingBox const* other) const;

		bool intersectsLine(Vector2 const& v0, Vector2 const& v1) const;

		bool intersectsLine(float v0x, float v0y, float v1x, float v1y) const;

		bool intersectsTriMesh(Triangulation const& triangles) const;

		// Utility
		BoundingCircle unionWith(BoundingCircle const& other);
	};

} // WP_NAMESPACE
