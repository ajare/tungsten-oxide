#include "willpower/common/WillpowerWalker.h"

#include "willpower/geometry/CsgUtils.h"
#include "willpower/geometry/clipper.hpp"

#undef max

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace std;
		using namespace WP_NAMESPACE;

		vector<CsgUtils::Polygon> CsgUtils::opUnion(vector<Polygon> const& polygons)
		{
			vector<Polygon> result;

			ClipperLib::Clipper clipper;

			for (uint32_t i = 0; i < polygons.size(); ++i)
			{
				ClipperLib::Path inPath;
				
				auto const& polygon = polygons[i];
				for (auto const& vertex : polygon)
				{
					inPath.push_back(ClipperLib::IntPoint((int)vertex.x, (int)vertex.y));
				}

				auto polyType = i == 0 ? 
					ClipperLib::PolyType::ptSubject : 
					ClipperLib::PolyType::ptClip;

				clipper.AddPath(inPath, polyType, true);
			}

			// Execute
			ClipperLib::Paths solution;
			clipper.Execute(
				ClipperLib::ClipType::ctUnion, 
				solution, 
				ClipperLib::PolyFillType::pftEvenOdd, 
				ClipperLib::PolyFillType::pftPositive);

			// Build result
			for (auto const& path : solution)
			{
				Polygon p;

				for (auto const& point : path)
				{
					p.push_back(Vector2((float)point.X, (float)point.Y));
				}

				result.push_back(p);
			}

			return result;
		}

	} // geometry
} // WP_NAMESPACE
