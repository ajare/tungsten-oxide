#include "willpower/geometry/BezierOffsetter.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace WP_NAMESPACE;
		using namespace std;

		BezierOffsetter::BezierOffsetter(vector<wp::Vector2> const& points, float maxMiter, float scale)
			: Offsetter(vector<wp::Vector2>(), maxMiter)
			, mCurve(points)
			, mScale(scale)
		{
		}

		void BezierOffsetter::offset(float amount1, float amount2, CornerType cornerType, WidthModificationFunction widthModifier, int startVertex, int endVertex)
		{
			mVertices = mCurve.divide(true, mScale);

			if (startVertex == endVertex)
			{
				endVertex = (int)mVertices.size() - 1;
			}

			auto side1 = offsetImpl(amount1, amount2, cornerType, widthModifier, startVertex, endVertex);
			auto side2 = offsetImpl(-amount1, -amount2, cornerType, widthModifier, startVertex, endVertex);
			reverse(side2.begin(), side2.end());

			mOutputVertices.clear();
			mOutputVertices.push_back(side1);
			mOutputVertices.push_back(side2);
		}

	} // geometry
} //WP_NAMESPACE
