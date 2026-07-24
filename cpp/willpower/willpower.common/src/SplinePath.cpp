#include "willpower/common/SplinePath.h"

using namespace std;

namespace WP_NAMESPACE
{

	SplinePath::SplinePath()
	{
	}

	SplinePath::SplinePath(vector<Vector2> const& points)
		: mPoints(points)
	{
		int numPoints = (int)points.size();
		if (numPoints < 4)
		{
			throw exception("SplinePath: cubic curves require at least 4 points.");
		}
	}

	int SplinePath::getNumControlPoints() const
	{
		return (int)mPoints.size();
	}

	Vector2 const& SplinePath::getControlPoint(int index) const
	{
		return mPoints[index];
	}

	void SplinePath::setControlPoint(int index, Vector2 const& position)
	{
		mPoints[index] = position;
		rebuildRenderable();
	}

	vector<Vector2> SplinePath::divide(bool adaptive, float scale) const
	{
		WP_UNUSED(adaptive);

		vector<Vector2> vertices;

		float length = getLength();

		int n = (int)(length * scale);
		float t = 0.0f, dt = 1.0f / (n - 1);

		for (int i = 0; i < n; ++i)
		{
			vertices.push_back(getPosition(t * length));
			t += dt;
		}

		return vertices;
	}

	BoundingBox SplinePath::getBounds() const
	{
		// Piecemeal approximate.
		BoundingBox bounds(divide(false));

		// Expand a little to allow for inaccuracies in the curve approximation
		bounds.expand(0.01f, 0.01f);

		return bounds;
	}


} // WP_NAMESPACE
