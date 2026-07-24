#include "willpower/common/MathsUtils.h"

#include "willpower/geometry/Offsetter.h"
#include "willpower/geometry/Exception.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace WP_NAMESPACE;
		using namespace std;

		Offsetter::Offsetter(vector<Vector2> const& vertices, float maxMiter)
			: mVertices(vertices)
			, mMaxMiter(maxMiter)
		{
			// Remove duplicate vertices from the end
			int i = (int)mVertices.size() - 2;
			while (i >= 0)
			{
				if (mVertices[i] == mVertices[i + 1])
				{
					mVertices.pop_back();
					--i;
				}
				else
				{
					break;
				}
			}
		}

		vector<Vector2> const& Offsetter::getVertices() const
		{
			return mVertices;
		}

		vector<vector<Vector2>> const& Offsetter::getOffsetVertices() const
		{
			return mOutputVertices;
		}

		void Offsetter::extrudeEdge(Edge& edge, Edge const* prev, Edge const* next, float distance)
		{
			WP_UNUSED(next);
			WP_UNUSED(prev);

			edge.v0 += edge.normal0 * distance;
			edge.v1 += edge.normal1 * distance;

			edge.vertices.clear();
			edge.vertices.push_back(edge.v0);
			edge.vertices.push_back(edge.v1);
		}

		void Offsetter::makeArcCorner(Edge& edge, Edge const* prev, Edge const* next, float distance, float segmentLength)
		{
			WP_UNUSED(next);
			WP_UNUSED(prev);

			float angle = (edge.v0 - edge.centre).anticlockwiseAngleTo(edge.v1 - edge.centre);
			float arcLength = WP_TWOPI * distance * angle / 360.0f;

			// Find closest number of segments to use based on segmentLength
			int numSegments = (int)(round(arcLength / segmentLength));
			float segmentAngle = angle / numSegments;

			Vector2 arcPos = edge.v0 - edge.centre;

			for (int i = 0; i < numSegments - 1; ++i)
			{
				arcPos.rotateAnticlockwise(segmentAngle);
				edge.vertices.push_back(edge.centre + arcPos);
			}
		}

		void Offsetter::makeUnmitredCorner(Edge& edge, Edge const* prev, Edge const* next, float distance)
		{
			LineHit hit;
			auto intersects = MathsUtils::rayRayIntersection(edge.v0, prev->dir, edge.v1, -next->dir, &hit);
			if (intersects == MathsUtils::LineIntersectionType::Intersecting)
			{
				if (hit.getTime() < 0)
				{
					throw exception("Corner angle is obtuse.");
				}

				auto const& hitPos = hit.getPosition();
				float iDist = edge.centre.distanceTo(hitPos);

				// Calculate central point on square line
				if (iDist > distance * mMaxMiter)
				{
					// Turn into a square
					makeSquareCorner(edge, prev, next, distance);
				}
				else
				{
					edge.vertices.push_back(hitPos);
				}
			}
			else
			{
				throw exception("Corner angle is obtuse.");
			}
		}

		void Offsetter::makeSquareCorner(Edge& edge, Edge const* prev, Edge const* next, float distance)
		{
			edge.type = Edge::Type::CornerSquare;

			Vector2 c = edge.v0.lerp(edge.v1, 0.5f);

			Vector2 centreDir = edge.centre.directionTo(c);
			Vector2 centre = edge.centre + centreDir * distance;

			// Calculate square points
			Vector2 centreExtrudeDir = centreDir.perpendicular();
			LineHit hit;
			auto ci0 = MathsUtils::rayRayIntersection(edge.v0, prev->dir, centre, -centreExtrudeDir, &hit);

			if (ci0 == MathsUtils::LineIntersectionType::Intersecting)
			{
				edge.vertices.push_back(hit.getPosition());
			}
			else
			{
				throw GeometryOperationException(__FUNCTION__, "Cannot create square corner.", false);
			}

			auto ci1 = MathsUtils::rayRayIntersection(edge.v1, -next->dir, centre, centreExtrudeDir, &hit);

			if (ci1 == MathsUtils::LineIntersectionType::Intersecting)
			{
				edge.vertices.push_back(hit.getPosition());
			}
			else
			{
				throw GeometryOperationException(__FUNCTION__, "Cannot create square corner.", false);
			}
		}

		void Offsetter::extrudeCorner(Edge& edge, Edge const* prev, Edge const* next, float distance)
		{
			if (!prev || !next)
			{
				throw exception("Corner is not between two edges.");
			}

			edge.v0 += edge.normal0 * distance;
			edge.v1 += edge.normal1 * distance;

			edge.vertices.clear();
			edge.vertices.push_back(edge.v0);

			switch (edge.type)
			{
			case Edge::Type::CornerArc:
				makeArcCorner(edge, prev, next, distance, 8);
				break;
			case Edge::Type::CornerSquare:
				makeSquareCorner(edge, prev, next, distance);
				break;
			case Edge::Type::CornerUnmitred:
				makeUnmitredCorner(edge, prev, next, distance);
				break;
			default:
				break;
			}

			edge.vertices.push_back(edge.v1);
		}

		Offsetter::Edge Offsetter::createCorner(CornerType cornerType, Edge const& prev, Edge const& next, int index)
		{
			WP_UNUSED(index);

			Edge edge;

			edge.type = Edge::Type::CornerUnknown;
			edge.v0 = prev.v1;
			edge.v1 = next.v0;
			edge.normal0 = prev.normal1;
			edge.normal1 = next.normal0;
			edge.centre = edge.v0;

			// Set type
			switch (cornerType)
			{
			case CornerType::Arc:
				edge.type = Edge::Type::CornerArc;
				break;
			case CornerType::Mitred:
				edge.type = Edge::Type::CornerUnmitred;
				break;
			case CornerType::Square:
				edge.type = Edge::Type::CornerSquare;
				break;
			}

			return edge;
		}

		bool Offsetter::isVertexCollinear(int i, Vector2 const& vertex, vector<Vector2> const& vertices)
		{
			int j = i == 0 ? (int)vertices.size() - 1 : i - 1;
			Vector2 dir0 = vertices[j].directionTo(vertices[i]);
			Vector2 dir1 = vertices[i].directionTo(vertex);

			float angle = dir0.minimumAngleTo(dir1);
			return angle < 0.5f;
		}

		void Offsetter::addClippedVertexToOutput(vector<Vector2>& outputVertices, Vector2 const& vertex)
		{
			// Check for collinearity.  If the direction from vertex n-1 to n is the
			// same as the direction from vertex n to this one, then remove the last one.
			int vertexCount = (int)outputVertices.size();
			if (vertexCount >= 2)
			{
				if (isVertexCollinear(vertexCount - 1, vertex, outputVertices))
				{
					outputVertices.pop_back();
				}
			}

			outputVertices.push_back(vertex);
		}

		void Offsetter::setClippedOutputVertex(vector<Vector2>& outputVertices, int index, Vector2 const& vertex)
		{
			outputVertices[index] = vertex;
		}

		Offsetter::IntersectionInfo Offsetter::checkIntersection(vector<Vector2> const& vertices, int i, int j)
		{
			LineHit hit;
			auto intersects = MathsUtils::lineLineIntersection(
				vertices[i * 2 + 0],
				vertices[i * 2 + 1],
				vertices[j * 2 + 0],
				vertices[j * 2 + 1],
				&hit);

			IntersectionInfo info;
			if (intersects == MathsUtils::LineIntersectionType::Intersecting)
			{
				info.hit = true;
				info.t = hit.getTime();
				info.edge = j;
				info.position = hit.getPosition();
			}
			else
			{
				info.hit = false;
			}

			return info;
		}

		void Offsetter::offsetEdges(vector<Edge>& edges, bool isLoop, float amount1, float amount2, WidthModificationFunction widthModifier)
		{
			int edgeCount = (int)edges.size() - 1;
			for (int i = 0; i <= edgeCount; ++i)
			{
				float t = i / (float)edges.size();
				auto& edge = edges[i];

				Edge const* prevEdge = i == 0 ? (isLoop ? &edges[edgeCount] : nullptr) : &edges[i - 1];
				Edge const* nextEdge = i == edgeCount ? (isLoop ? &edges[0] : nullptr) : &edges[i + 1];

				float amount = amount1 + (amount2 - amount1) * t;
				amount *= widthModifier(t);

				switch (edge.type)
				{
				case Edge::Type::Straight:
					extrudeEdge(edge, prevEdge, nextEdge, amount);
					break;
				case Edge::Type::CornerArc:
				case Edge::Type::CornerSquare:
				case Edge::Type::CornerUnmitred:
					extrudeCorner(edge, prevEdge, nextEdge, amount);
				default:
					break;
				}
			}
		}

		void Offsetter::addEdgeAndCorner(int i, vector<Edge>& edges, vector<Vector2> const& vertices, CornerType cornerType, int normalDir)
		{
			Edge edge;

			int j = (i + 1) % vertices.size();

			edge.type = Edge::Type::Straight;
			edge.v0 = vertices[i];
			edge.v1 = vertices[j];
			edge.dir = (vertices[j] - vertices[i]).normalisedCopy();
			edge.normal0 = edge.normal1 = edge.dir.perpendicular() * (float)normalDir;
			edge.centre = edge.v0.lerp(edge.v1, 0.5f);

			// Do we want to insert a corner edge before this? Ie after the last one.
			if (i > 0)
			{
				float angle = edge.normal0.anticlockwiseAngleTo(edges.back().normal1);
				if (normalDir > 0)
				{
					angle = 360.0f - angle;
				}

				if (angle > 180 && angle < 360)
				{
					Edge cornerEdge = createCorner(cornerType, edges.back(), edge, i);
					edges.push_back(cornerEdge);
				}
			}

			edges.push_back(edge);
		}

		std::vector<Vector2> Offsetter::offsetImpl(float amount1, float amount2, CornerType cornerType, WidthModificationFunction widthModifier, int startVertex, int endVertex)
		{
			std::vector<Vector2> outputVertices;

			if (amount1 == 0 && amount2 == 0)
			{
				return mVertices;
			}

			if (amount1 < 0 && amount2 > 0 || amount1 > 0 && amount2 < 0)
			{
				throw GeometryOperationException(__FUNCTION__, "amounts cannot have opposite signs.", true);
			}

			int normalDir = -1;
			if (amount1 < 0.0f)
			{
				amount1 = -amount1;
				amount2 = -amount2;
				normalDir = 1;
			}

			if (endVertex < 0)
			{
				endVertex = (int)mVertices.size() - 1;
			}

			if (startVertex > endVertex)
			{
				return vector<Vector2>();
			}

			bool isLoop = startVertex == endVertex;

			// Rotate vector
			vector<Vector2> vertices(mVertices.begin(), mVertices.begin() + startVertex);
			vector<Vector2> verticesEnd(mVertices.begin() + startVertex, mVertices.end());
			vertices.insert(vertices.begin(), verticesEnd.begin(), verticesEnd.end());

			vector<Edge> edges;

			// Generate edge positions and normals
			int vertexCount = isLoop ? (int)mVertices.size() - 1 : endVertex - startVertex;
			for (int i = 0; i < vertexCount; ++i)
			{
				addEdgeAndCorner(i, edges, vertices, cornerType, normalDir);
			}
			if (isLoop)
			{
				addEdgeAndCorner(vertexCount, edges, vertices, cornerType, normalDir);

				// Insert final corner
				float angle = edges.front().normal0.anticlockwiseAngleTo(edges.back().normal1);
				if (normalDir > 0)
					angle = 360.0f - angle;
				if (angle > 180)
				{
					Edge cornerEdge = createCorner(cornerType, edges.back(), edges.front(), 0);
					edges.push_back(cornerEdge);
				}
			}

			// Offset them
			offsetEdges(edges, isLoop, amount1, amount2, widthModifier);

			// Create vertex list to be clipped
			vector<Vector2> unclippedVertices;
			for (auto const& edge : edges)
			{
				for (int i = 0; i < (int)edge.vertices.size() - 1; ++i)
				{
					unclippedVertices.push_back(edge.vertices[i + 0]);
					unclippedVertices.push_back(edge.vertices[i + 1]);
				}
			}

			// Go through vertices one edge at a time, and test the intersection of edge i with i + 2 up
			// to N (or N-1 if a loop).  If an intersection is found, mark its distance and the edge index.
			// Then take the first intersection point (ie closes to first vertex), replace the end and start
			// vertices with it, and continue.
			int i = 0, vertexMax = (int)unclippedVertices.size() / 2;
			while (i < vertexMax)
			{
				// Get closest intersection
				IntersectionInfo closest;
				for (int j = i + 1; j < vertexMax; ++j)
				{
					auto edgeInfo = checkIntersection(unclippedVertices, i, j);
					if (edgeInfo.hit)
					{
						int prevEdge = i > 0 ? i - 1 : vertexMax - 1;
						if (edgeInfo.t < closest.t && j != prevEdge)
						{
							closest = edgeInfo;
						}
					}
				}
				if (isLoop && i == (vertexMax - 1))
				{
					auto edgeInfo = checkIntersection(unclippedVertices, i, 0);
					if (edgeInfo.hit)
					{
						closest = edgeInfo;
					}
				}

				if (closest.edge >= 0)
				{
					addClippedVertexToOutput(outputVertices, unclippedVertices[i * 2]);
					unclippedVertices[closest.edge * 2] = closest.position;
					i = closest.edge;

					if (i == 0)
					{
						setClippedOutputVertex(outputVertices, 0, closest.position);
						break;
					}
				}
				else
				{
					addClippedVertexToOutput(outputVertices, unclippedVertices[i * 2 + 0]);
					i = i + 1;
				}
			}

			// Final vertex to close the polyline
			addClippedVertexToOutput(outputVertices, isLoop ? unclippedVertices.front() : unclippedVertices.back());

			// Check whether last or first vertices need to be removed due to collinearity
			if (isLoop)
			{
				// Last vertex is the same as the first, so check last and second
				if (isVertexCollinear((int)outputVertices.size() - 1, outputVertices[1], outputVertices))
				{
					// Remove front and back, then add new front onto back
					rotate(outputVertices.begin(), outputVertices.begin() + 1, outputVertices.end());
					outputVertices.pop_back();
					outputVertices.pop_back();
					outputVertices.push_back(outputVertices.front());
				}
			}

			return outputVertices;
		}

	} // geometry
} //WP_NAMESPACE
