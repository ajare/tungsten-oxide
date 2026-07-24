#pragma once
#include <vector>
#include <functional>

#include "willpower/common/Vector2.h"

#include "willpower/geometry/Platform.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class Offsetter
		{
			struct IntersectionInfo
			{
				bool hit;
				float t;
				int edge;
				wp::Vector2 position;

				IntersectionInfo()
					: hit(false)
					, t(1.1f)
					, edge(-1)
				{
				}
			};

			struct Edge
			{
				enum class Type
				{
					Straight,		// Regular edge
					CornerUnknown,	// To-be-decided corner type
					CornerArc,		// Rounded corner connecting two edges
					CornerUnmitred,	// Unmitred (ie sharp) corner connecting two edges
					CornerSquare	// Square (ie flattened-off) corner connecting two edges
				};

				Type type;
				wp::Vector2 v0, v1, dir, normal0, normal1;

				wp::Vector2 centre;
				std::vector<wp::Vector2> vertices;
			};

		public:

			enum class CornerType
			{
				Arc,
				Mitred,
				Square
			};

			typedef std::function<float(float)> WidthModificationFunction;

		private:

			float mMaxMiter;

		protected:

			std::vector<wp::Vector2> mVertices;

			std::vector<std::vector<wp::Vector2>> mOutputVertices;

		private:

			bool isVertexCollinear(int i, wp::Vector2 const& vertex, std::vector<wp::Vector2> const& vertices);

			void addEdgeAndCorner(int i, std::vector<Edge>& edges, std::vector<wp::Vector2> const& vertices, CornerType cornerType, int normalDir);

			void offsetEdges(std::vector<Edge>& edges, bool isLoop, float amount1, float amount2, WidthModificationFunction widthModifier);

			void addClippedVertexToOutput(std::vector<wp::Vector2>& outputVertices, wp::Vector2 const& vertex);

			void setClippedOutputVertex(std::vector<wp::Vector2>& outputVertices, int index, wp::Vector2 const& vertex);

			IntersectionInfo checkIntersection(std::vector<wp::Vector2> const& vertices, int i, int j);

			void extrudeEdge(Edge& edge, Edge const* prev, Edge const* next, float distance);

			void makeArcCorner(Edge& edge, Edge const* prev, Edge const* next, float distance, float segmentLength);

			void makeUnmitredCorner(Edge& edge, Edge const* prev, Edge const* next, float distance);

			void makeSquareCorner(Edge& edge, Edge const* prev, Edge const* next, float distance);

			void extrudeCorner(Edge& edge, Edge const* prev, Edge const* next, float distance);

			Edge createCorner(CornerType cornerType, Edge const& prev, Edge const& next, int index);

		protected:
			
			std::vector<wp::Vector2> offsetImpl(float amount1, float amount2, CornerType cornerType, WidthModificationFunction widthModifier, int startVertex = 0, int endVertex = -1);

		public:

			Offsetter(std::vector<wp::Vector2> const& vertices, float maxMiter);

			std::vector<wp::Vector2> const& getVertices() const;

			std::vector<std::vector<wp::Vector2>> const& getOffsetVertices() const;

			virtual void offset(float amount1, float amount2, CornerType cornerType, WidthModificationFunction widthModifier = defaultWidthModifier, int startVertex = 0, int endVertex = -1) = 0;

			static float defaultWidthModifier(float t)
			{
				WP_UNUSED(t);
				return 1.0f;
			}
		};

	} // geometry
} // WP_NAMESPACE
