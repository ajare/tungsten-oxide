#pragma once

#include <vector>

#include "willpower/common/Platform.h"
#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingBox.h"
#include "willpower/common/Renderable.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API SplinePath : public Renderable
	{
	protected:

		std::vector<Vector2> mPoints;

	public:

		SplinePath();

		explicit SplinePath(std::vector<Vector2> const& points);

		virtual std::vector<Vector2> divide(bool adaptive, float scale = 1.0f) const;

		int getNumControlPoints() const;

		Vector2 const& getControlPoint(int index) const;

		virtual void setControlPoint(int index, Vector2 const& position);

		virtual Vector2 getPosition(float distance) const = 0;

		virtual Vector2 getDirection(float distance) const = 0;

		virtual Vector2 getAcceleration(float distance) const = 0;

		virtual float getLength() const = 0;

		virtual BoundingBox getBounds() const;
	};

} // WP_NAMESPACE

