#pragma once

#include <vector>

#include "willpower/common/Platform.h"
#include "willpower/common/Vector2.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API Triangulation
	{
		std::vector<Vector2> mVertices;

	private:

		void copyFrom(Triangulation const& other);

		void clear();

	public:

		Triangulation();

		Triangulation(Triangulation const& other);

		Triangulation& operator=(Triangulation const& other);
		
		void addTriangle(Vector2 const& v0, Vector2 const& v1, Vector2 const& v2);

		uint32_t getNumTriangles() const;

		void getTriangle(uint32_t index, Vector2& v0, Vector2& v1, Vector2& v2) const;

		bool intersects(Triangulation const& other) const;
	};

} // WP_NAMESPACE
