#pragma once

#include <vector>

#include "willpower/common/Platform.h"
#include "willpower/common/Vector2.h"
#include "willpower/common/Triangulation.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API BoundingConvexPolygon
	{
		std::vector<Vector2> mVertices;

		Vector2 mPosition;

		Vector2 mCentre;

		Vector2 mMinExtent, mMaxExtent;

	private:

		void updateExtents();

	public:

		BoundingConvexPolygon();

		BoundingConvexPolygon(Vector2 const& position, std::vector<Vector2> vertices);

		void setPosition(Vector2 const& position);

		void setPosition(float x, float y);

		Vector2 const& getPosition() const;

		void move(Vector2 const& distance);

		void move(float x, float y);

		Vector2 const& getCentre() const;

		void getExtents(Vector2& minExtent, Vector2& maxExtent) const;

		Vector2 const& getMinExtent() const;

		Vector2 const& getMaxExtent() const;

		// Intersection tests
		bool pointInside(Vector2 const& point) const;

		bool pointInside(float x, float y) const;

		bool intersectsTriMesh(Triangulation const& triangles) const;
	};

} // WP_NAMESPACE
