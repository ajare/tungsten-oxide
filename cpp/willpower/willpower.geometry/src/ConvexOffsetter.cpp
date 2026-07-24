#include "willpower/geometry/ConvexOffsetter.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace WP_NAMESPACE;
		using namespace std;

		ConvexOffsetter::ConvexOffsetter(vector<wp::Vector2> const& vertices, float maxMiter)
			: Offsetter(vertices, maxMiter)
		{
		}

		void ConvexOffsetter::offset(float amount1, float amount2, CornerType cornerType, WidthModificationFunction widthModifier, int startVertex, int endVertex)
		{
			mOutputVertices.clear();
			mOutputVertices.push_back(offsetImpl(amount1, amount2, cornerType, widthModifier, startVertex, endVertex));
		}

	} // geometry
} //WP_NAMESPACE
