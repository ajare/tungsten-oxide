#include <cassert>
#include <algorithm>
#include <map>

#include "willpower/common/MathsUtils.h"
#include "willpower/common/Triangulation.h"

namespace WP_NAMESPACE
{

	using namespace std;

	Triangulation::Triangulation()
	{
	}

	Triangulation::Triangulation(Triangulation const& other)
	{
		copyFrom(other);
	}

	Triangulation& Triangulation::operator=(Triangulation const& other)
	{
		copyFrom(other);
		return *this;
	}

	void Triangulation::copyFrom(Triangulation const& other)
	{
		mVertices = other.mVertices;
	}

	void Triangulation::clear()
	{
		mVertices.clear();
	}

	void Triangulation::addTriangle(Vector2 const& v0, Vector2 const& v1, Vector2 const& v2)
	{
		mVertices.push_back(v0);
		mVertices.push_back(v1);
		mVertices.push_back(v2);
	}

	uint32_t Triangulation::getNumTriangles() const
	{
		return (uint32_t)mVertices.size() / 3;
	}

	void Triangulation::getTriangle(uint32_t index, Vector2& v0, Vector2& v1, Vector2& v2) const
	{
		v0 = mVertices[index * 3 + 0];
		v1 = mVertices[index * 3 + 1];
		v2 = mVertices[index * 3 + 2];
	}

	bool Triangulation::intersects(Triangulation const& other) const
	{
		for (uint32_t i = 0; i < getNumTriangles(); ++i)
		{
			Vector2 v00, v01, v02;
			getTriangle(i, v00, v01, v02);

			for (uint32_t j = 0; j < other.getNumTriangles(); ++j)
			{
				Vector2 v10, v11, v12;
				other.getTriangle(j, v10, v11, v12);

				if (MathsUtils::triangleIntersectsTriangle(v00, v01, v02, v10, v11, v12))
				{
					return true;
				}
			}
		}

		return false;
	}

} // WP_NAMESPACE
