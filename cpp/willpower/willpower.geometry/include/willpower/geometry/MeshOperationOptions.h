#pragma once

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Types.h"
#include "willpower/geometry/Offsetter.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		struct BridgeEdgesOptions
		{
			enum class Type
			{
				Straight,
				Curved
			};

			enum class SqueezeType
			{
				None,
				Straight,
				Curved,
			};

			Type type;
			SqueezeType squeezeType;
			float squeezeAmount;
			int steps;
			bool merge;
			float tightness;

		public:

			BridgeEdgesOptions()
				: BridgeEdgesOptions(Type::Straight, SqueezeType::None, 0.0f, 1, false, 0.551915f)
			{
			}

			BridgeEdgesOptions(Type t, SqueezeType squeeze, float squeezeAmt, int stps, bool mrg, float tight)
				: type(t)
				, squeezeType(squeeze)
				, squeezeAmount(squeezeAmt)
				, steps(stps)
				, merge(mrg)
				, tightness(tight)
			{
			}
		};

		struct WeldEdgesOptions
		{
			bool mergePolygons;
			bool moveFirstToSecond;

		public:

			WeldEdgesOptions()
				: WeldEdgesOptions(false, false)
			{
			}

			WeldEdgesOptions(bool merge, bool move)
				: mergePolygons(merge)
				, moveFirstToSecond(move)
			{
			}
		};

		struct ExtrudePolygonOptions
		{
			Offsetter::CornerType cornerType;
			bool mergePolygons;
			float chamfer;
			bool separateExtrusions;
			bool removeCollinearVertices;

		public:

			ExtrudePolygonOptions()
				: ExtrudePolygonOptions(Offsetter::CornerType::Square, false, 0.0f, false, false)
			{
			}

			ExtrudePolygonOptions(Offsetter::CornerType corner, float chamferAmt, bool mergePolys, bool separate, bool removeCollinear)
				: cornerType(corner)
				, mergePolygons(mergePolys)
				, chamfer(chamferAmt)
				, separateExtrusions(separate)
				, removeCollinearVertices(removeCollinear)
			{
			}
		};

		struct ExtrudeVertexOptions
		{
			enum class Type
			{
				Round,
				Square
			};

			Type type;
			bool outwards;
			float squareThreshold;

		public:

			ExtrudeVertexOptions()
				: ExtrudeVertexOptions(Type::Round, true, 3.0f)
			{
			}

			explicit ExtrudeVertexOptions(Type t, bool out, float sqThreshold)
				: type(t)
				, outwards(out)
				, squareThreshold(sqThreshold)
			{
			}
		};

	} // geometry
} // WP_NAMESPACE

