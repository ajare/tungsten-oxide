#include <cassert>
#include <algorithm>

#include "willpower/common/WillpowerWalker.h"
#include "willpower/common/MathsUtils.h"
#include "willpower/common/TriangleIntersection.h"

#undef min
#undef max

#define EQ_EPSILON(x,y) (x >= ((y) - MathsUtils::Epsilon) && x <= ((y) + MathsUtils::Epsilon))
#define LTE_EPSILON(x,y) (x <= ((y) + MathsUtils::Epsilon))
#define GTE_EPSILON(x,y) (x >= ((y) - MathsUtils::Epsilon))

namespace WP_NAMESPACE
{

	using namespace std;

	float MathsUtils::Epsilon = 0.0001f;

	int MathsUtils::modulo(int x, int n)
	{
		return (x % n + n) % n;
	}

	float MathsUtils::degrees(float radians)
	{
		return WP_RADTODEG(radians);
	}

	float MathsUtils::radians(float degrees)
	{
		return WP_DEGTORAD(degrees);
	}

	MathsUtils::Side MathsUtils::pointSideOnLine(Vector2 const& point, Vector2 const& linev0, Vector2 const& linev1)
	{
		float f = (linev1.y - linev0.y) * point.x + (linev0.x - linev1.x) * point.y + (linev1.x * linev0.y - linev0.x * linev1.y);
		return f < 0.0f ? Side::Left : (f > 0.0f ? Side::Right : Side::On);
	}

	MathsUtils::Side MathsUtils::pointSideOnLine(float x, float y, Vector2 const& linev0, Vector2 const& linev1)
	{
		return pointSideOnLine(Vector2(x, y), linev0, linev1);
	}

	wp::Winding MathsUtils::pointsWinding(vector<Vector2> const& points)
	{
		ASSERT_TRACE(points.size() >= 3 && "MathsUtils::pointsWinding() too few points to determine winding direction.");
		float sum = 0.0f;

		auto numPoints = (uint32_t)points.size();
		for (uint32_t i = 0; i < numPoints; ++i)
		{
			auto const& point1 = points[i];
			auto const& point2 = points[(i + 1) % numPoints];

			sum += (point2.x - point1.x) * (point2.y + point1.y);
		}

		return sum >= 0 ? Winding::Clockwise : Winding::Anticlockwise;
	}

	bool MathsUtils::pointOnLine(Vector2 const& point, Vector2 const& linev0, Vector2 const& linev1, float maxDistance)
	{
		return point.distanceToLine(linev0, linev1) < maxDistance;
	}

	bool MathsUtils::pointsFormLine(Vector2 const& point1, Vector2 const& point2, Vector2 const& point3)
	{
		float area = fabs(point1.x * (point2.y - point3.y) + point2.x * (point3.y - point1.y) + point3.x * (point1.y - point2.y));
		return area < Epsilon;
	}

	bool MathsUtils::pointsFormLine(vector<Vector2> const& points, int offset)
	{
		auto numPoints = (uint32_t)points.size();
		ASSERT_TRACE(numPoints > 2 && "MathsUtils::pointsFormLine() too few points to determine if line is formed.");

		auto const& point1 = points[offset + 0];
		auto const& point2 = points[(offset + 1) % numPoints];
		auto const& point3 = points[(offset + 2) % numPoints];

		return pointsFormLine(point1, point2, point3);
	}

	void MathsUtils::getLineSweepHull(Vector2 const& linev0, Vector2 const& linev1, wp::Vector2 const& point, Vector2 const& halfSize, Vector2& ev0, Vector2& ev1, Vector2& nv0, Vector2& nv1, Vector2& fv0, Vector2& fv1)
	{
		MathsUtils::Side lineSide = pointSideOnLine(point, linev0, linev1);

		Vector2 dv = linev1 - linev0;

		if (dv.x > 0)
		{
			if (dv.y > 0)
			{
				ev0 = linev0 - halfSize;
				ev1 = linev1 + halfSize;

				if (lineSide == Side::Left)
				{
					nv0.x = linev0.x - halfSize.x; nv0.y = linev0.y + halfSize.y;
					nv1.x = linev1.x - halfSize.x; nv1.y = linev1.y + halfSize.y;
					fv0.x = linev0.x + halfSize.x; fv0.y = linev0.y - halfSize.y;
					fv1.x = linev1.x + halfSize.x; fv1.y = linev1.y - halfSize.y;
				}
				else
				{
					fv0.x = linev0.x - halfSize.x; fv0.y = linev0.y + halfSize.y;
					fv1.x = linev1.x - halfSize.x; fv1.y = linev1.y + halfSize.y;
					nv0.x = linev0.x + halfSize.x; nv0.y = linev0.y - halfSize.y;
					nv1.x = linev1.x + halfSize.x; nv1.y = linev1.y - halfSize.y;
				}
			}
			else
			{
				ev0.x = linev0.x - halfSize.x; ev0.y = linev0.y + halfSize.y;
				ev1.x = linev1.x + halfSize.x; ev1.y = linev1.y - halfSize.y;

				if (lineSide == Side::Left)
				{
					nv0 = linev0 + halfSize;
					nv1 = linev1 + halfSize;
					fv0 = linev0 - halfSize;
					fv1 = linev1 - halfSize;
				}
				else
				{
					fv0 = linev0 + halfSize;
					fv1 = linev1 + halfSize;
					nv0 = linev0 - halfSize;
					nv1 = linev1 - halfSize;
				}
			}
		}
		else
		{
			if (dv.y > 0)
			{
				ev0.x = linev0.x + halfSize.x; ev0.y = linev0.y - halfSize.y;
				ev1.x = linev1.x - halfSize.x; ev1.y = linev1.y + halfSize.y;

				if (lineSide == Side::Left)
				{
					fv0 = linev0 + halfSize;
					fv1 = linev1 + halfSize;
					nv0 = linev0 - halfSize;
					nv1 = linev1 - halfSize;
				}
				else
				{
					nv0 = linev0 + halfSize;
					nv1 = linev1 + halfSize;
					fv0 = linev0 - halfSize;
					fv1 = linev1 - halfSize;
				}
			}
			else
			{
				ev0 = linev0 + halfSize;
				ev1 = linev1 - halfSize;

				if (lineSide == Side::Left)
				{
					nv0.x = linev0.x + halfSize.x; nv0.y = linev0.y - halfSize.y;
					nv1.x = linev1.x + halfSize.x; nv1.y = linev1.y - halfSize.y;
					fv0.x = linev0.x - halfSize.x; fv0.y = linev0.y + halfSize.y;
					fv1.x = linev1.x - halfSize.x; fv1.y = linev1.y + halfSize.y;
				}
				else
				{
					nv0.x = linev0.x - halfSize.x; nv0.y = linev0.y + halfSize.y;
					nv1.x = linev1.x - halfSize.x; nv1.y = linev1.y + halfSize.y;
					fv0.x = linev0.x + halfSize.x; fv0.y = linev0.y - halfSize.y;
					fv1.x = linev1.x + halfSize.x; fv1.y = linev1.y - halfSize.y;
				}
			}
		}
	}

	float MathsUtils::anticlockwiseAngleBetween(Vector2 const& v0, Vector2 const& v1, Vector2 const& v2)
	{
		Vector2 d0 = v0 - v1;
		Vector2 d1 = v2 - v1;
		return d0.anticlockwiseAngleTo(d1);
	}

	float MathsUtils::clockwiseAngleBetween(Vector2 const& v0, Vector2 const& v1, Vector2 const& v2)
	{
		Vector2 d0 = v0 - v1;
		Vector2 d1 = v2 - v1;
		return d0.clockwiseAngleTo(d1);
	}

	bool MathsUtils::polygonIsConvex(vector<Vector2> const& vertices)
	{
		auto c = (uint32_t)vertices.size();

		if (c < 4)
		{
			return true;
		}

		bool sign = false;
		for (uint32_t i = 0; i < c; ++i)
		{
			Vector2 d1 = vertices[(i + 1) % c] - vertices[i];
			Vector2 d2 = vertices[(i + 2) % c] - vertices[(i + 1) % c];

			float cp = d1.x * d2.y - d1.y * d2.x;

			if (i == 0)
			{
				sign = cp > 0;
			}
			else
			{
				if (sign != (cp > 0))
				{
					return false;
				}
			}
		}

		return true;
	}

	float MathsUtils::convexPolygonArea(vector<Vector2> const& vertices)
	{
		float area = 0.0f;

		auto numVertices = (uint32_t)vertices.size();
		for (uint32_t i = 0, j = numVertices - 1; i < numVertices; j = i++)
		{
			area += (vertices[j].x + vertices[i].x) - (vertices[j].y - vertices[i].y);
		}

		// This calculates for clockwise winding, so invert.
		return -area;
	}

	void MathsUtils::barycentricCoords(Vector2 const& p, Vector2 const& v0, Vector2 const& v1, Vector2 const& v2, float& u, float& v, float& w)
	{
		wp::Vector2 vd0 = v1 - v0, vd1 = v2 - v0, vd2 = p - v0;
		float denom = vd0.x * vd1.y - vd1.x * vd0.y;

		v = (vd2.x * vd1.y - vd1.x * vd2.y) / denom;
		w = (vd0.x * vd2.y - vd2.x * vd0.y) / denom;
		u = 1.0f - v - w;
	}

	BoundingBox MathsUtils::getPolygonBounds(std::vector<Vector2> const& vertices)
	{
	BoundingBox bb;

	float x0 = numeric_limits<float>::max(), y0 = numeric_limits<float>::max();
	float x1 = numeric_limits<float>::lowest(), y1 = numeric_limits<float>::lowest();

	for (auto const& vertex : vertices)
	{
		if (vertex.x < x0) x0 = vertex.x;
		if (vertex.y < y0) y0 = vertex.y;
		if (vertex.x > x1) x1 = vertex.x;
		if (vertex.y > y1) y1 = vertex.y;
	}

	bb.setPosition(x0, y0);
	bb.setSize(x1 - x0, y1 - y0);
	return bb;
	}

	float MathsUtils::triangleArea(Vector2 const& p0, Vector2 const& p1, Vector2 const& p2)
	{
		float d = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
		return d / 2.0f;
	}

	vector<Vector2> MathsUtils::clipPolygonAgainstPolygon(vector<Vector2> const& points, vector<Vector2> const& clipper)
	{
		vector<Vector2> newPoints = points, clipperPoints = clipper;

		// Clipper points should be clockwise
		Winding winding = pointsWinding(clipper);
		if (winding == Winding::Anticlockwise)
		{
			reverse(clipperPoints.begin(), clipperPoints.end());
		}

		winding = pointsWinding(newPoints);
		if (winding == Winding::Anticlockwise)
		{
			reverse(newPoints.begin(), newPoints.end());
		}

		// Go through each clipping line
		int clipperSize = (int)clipperPoints.size();
		for (int i = 0; i < clipperSize; ++i)
		{
			int j = (i + 1) % clipperSize;

			Vector2 c0 = clipperPoints[i];
			Vector2 c1 = clipperPoints[j];

			// Go through each line to be clipped
			vector<Vector2> result;
			int pointsSize = (int)newPoints.size();
			for (int k = 0; k < pointsSize; ++k)
			{
				int l = (k + 1) % pointsSize;

				Vector2 p0 = newPoints[k];
				Vector2 p1 = newPoints[l];

				// Calculate side of clipping line for each point
				float p0Side = (c1.x - c0.x) * (p0.y - c0.y) - (c1.y - c0.y) * (p0.x - c0.x);
				float p1Side = (c1.x - c0.x) * (p1.y - c0.y) - (c1.y - c0.y) * (p1.x - c0.x);

				if (p0Side < 0 && p1Side < 0)
				{
					// Both points are inside: add second point
					result.push_back(p1);
				}
				else if (p0Side >= 0 && p1Side < 0)
				{
					// Only first point is outside: point of intersection with edge and the second point is added
					float nx = xIntersect(c0, c1, p0, p1);
					float ny = yIntersect(c0, c1, p0, p1);

					result.push_back(Vector2(nx, ny));
					result.push_back(p1);
				}
				else if (p0Side < 0 && p1Side >= 0)
				{
					// Only second point is outside: point of intersection with edge is added.
					float nx = xIntersect(c0, c1, p0, p1);
					float ny = yIntersect(c0, c1, p0, p1);

					result.push_back(Vector2(nx, ny));
				}
			}

			newPoints = result;
		}

		return newPoints;
	}

	vector<float> MathsUtils::clipPolygonVertexDataAgainstPolygon(vector<float> const& vertexData, int vertexSize, vector<Vector2> const& clipper)
	{
		// Split data into separate vertices
		struct Vertex
		{
			Vector2 position;
			vector<float> data;
		};

		vector<Vertex> vertices;
		vector<Vector2> vertexPositions;

		for (uint32_t i = 0; i < vertexData.size(); i += vertexSize)
		{
			Vertex v;
			
			v.position.x = vertexData[i + 0];
			v.position.y = vertexData[i + 1];

			copy(vertexData.begin() + i + 2, vertexData.begin() + i + vertexSize, back_inserter(v.data));

			vertexPositions.push_back(v.position);
			vertices.push_back(v);
		}

		// Clipper points should be clockwise
		vector<Vector2> clipperPoints = clipper;

		Winding winding = pointsWinding(clipper);
		if (winding == Winding::Anticlockwise)
		{
			reverse(clipperPoints.begin(), clipperPoints.end());
		}

		// As should vertices
		winding = pointsWinding(vertexPositions);
		if (winding == Winding::Anticlockwise)
		{
			reverse(vertices.begin(), vertices.end());
		}

		// Clip
		vector<Vertex> clippedVertices;

		// Go through each clipping line
		int clipperSize = (int)clipperPoints.size();
		for (int i = 0; i < clipperSize; ++i)
		{
			int j = (i + 1) % clipperSize;

			Vector2 c0 = clipperPoints[i];
			Vector2 c1 = clipperPoints[j];

			// Go through each line to be clipped
			vector<Vector2> result;
			int pointsSize = (int)vertices.size();
			for (int k = 0; k < pointsSize; ++k)
			{
				int l = (k + 1) % pointsSize;

				Vertex const& v0 = vertices[k];
				Vertex const& v1 = vertices[l];

				Vector2 p0 = v0.position;
				Vector2 p1 = v1.position;

				// Calculate side of clipping line for each point
				float p0Side = (c1.x - c0.x) * (p0.y - c0.y) - (c1.y - c0.y) * (p0.x - c0.x);
				float p1Side = (c1.x - c0.x) * (p1.y - c0.y) - (c1.y - c0.y) * (p1.x - c0.x);

				if (p0Side < 0 && p1Side < 0)
				{
					// Both points are inside: add second point
					clippedVertices.push_back(v1);
				}
				else if (p0Side >= 0 && p1Side < 0)
				{
					// Only first point is outside: point of intersection with edge and the second point is added
					float nx = xIntersect(c0, c1, p0, p1);
					float ny = yIntersect(c0, c1, p0, p1);
					Vector2 pn = Vector2(nx, ny);

					// First vertex
					float t = p0.invLerp(pn, p1);
					Vertex nv;

					nv.position = pn;
					for (uint32_t m = 0; m < v0.data.size(); ++m)
					{
						float n = v0.data[m] + (v1.data[m] - v0.data[m]) * t;
						nv.data.push_back(n);
					}

					// Second vertex
					clippedVertices.push_back(v1);
				}
				else if (p0Side < 0 && p1Side >= 0)
				{
					// Only second point is outside: point of intersection with edge is added.
					float nx = xIntersect(c0, c1, p0, p1);
					float ny = yIntersect(c0, c1, p0, p1);
					Vector2 pn = Vector2(nx, ny);

					float t = p0.invLerp(pn, p1);
					Vertex nv;

					nv.position = pn;
					for (uint32_t m = 0; m < v0.data.size(); ++m)
					{
						float n = v0.data[m] + (v1.data[m] - v0.data[m]) * t;
						nv.data.push_back(n);
					}
				}
			}

			vertices = clippedVertices;
			clippedVertices.clear();
		}

		// Build clipped data
		vector<float> newVertexData;

		for (auto const& vertex: vertices)
		{
			newVertexData.push_back(vertex.position.x);
			newVertexData.push_back(vertex.position.y);
			for (int i = 0; i < (int)vertex.data.size(); ++i)
			{
				newVertexData.push_back(vertex.data[i]);
			}
		}
		
		return newVertexData;
	}

	vector<Vector2> MathsUtils::clipLineAgainstQuad(Vector2 const& v0, Vector2 const& v1, Vector2 const& q0, Vector2 const& q1)
	{
		vector<Vector2> newPoints;

		if (pointInBox(v0, q0, q1))
		{
			newPoints.push_back(v0);
		}
			
		if (pointInBox(v1, q0, q1))
		{
			newPoints.push_back(v1);
		}

		if (newPoints.size() == 2)
		{
			return newPoints;
		}

		Vector2 q10(q1.x, q0.y);
		Vector2 q01(q0.x, q1.y);

		LineHit lineHit;
		if (lineLineIntersection(v0, v1, q0, q10, &lineHit))
		{
			newPoints.push_back(lineHit.getPosition());
			if (newPoints.size() == 2)
			{
				return newPoints;
			}
		}
		if (lineLineIntersection(v0, v1, q10, q1, &lineHit))
		{
			newPoints.push_back(lineHit.getPosition());
			if (newPoints.size() == 2)
			{
				return newPoints;
			}
		}
		if (lineLineIntersection(v0, v1, q1, q01, &lineHit))
		{
			newPoints.push_back(lineHit.getPosition());
			if (newPoints.size() == 2)
			{
				return newPoints;
			}
		}
		if (lineLineIntersection(v0, v1, q01, q0, &lineHit))
		{
			newPoints.push_back(lineHit.getPosition());
			if (newPoints.size() == 2)
			{
				return newPoints;
			}
		}

		return newPoints;
	}

	vector<float> MathsUtils::clipLineVertexDataAgainstQuad(vector<float> const& vertexData, int vertexSize, Vector2 const& q0, Vector2 const& q1)
	{
		vector<float> newVertexData;

		// Add the end points directly if they're inside
		Vector2 p0(vertexData[0], vertexData[1]);
		Vector2 p1(vertexData[0 + vertexSize], vertexData[1 + vertexSize]);
		int verticesAdded = 0;

		if (pointInBox(p0, q0, q1))
		{
			newVertexData.push_back(p0.x);
			newVertexData.push_back(p0.y);
			for (int i = 2; i < vertexSize; ++i)
			{
				newVertexData.push_back(vertexData[i]);
			}

			verticesAdded++;
		}

		if (pointInBox(p1, q0, q1))
		{
			newVertexData.push_back(p1.x);
			newVertexData.push_back(p1.y);
			for (int i = vertexSize + 2; i < vertexSize * 2; ++i)
			{
				newVertexData.push_back(vertexData[i]);
			}

			verticesAdded++;
		}

		// If both are added, exit now
		if (verticesAdded == 2)
		{
			return newVertexData;
		}

		// Test intersections
		Vector2 q10(q1.x, q0.y);
		Vector2 q01(q0.x, q1.y);

		vector<Vector2> quadSides = {
			q0, q10,
			q10, q1,
			q1, q01,
			q01, q0
		};

		LineHit lineHit;
		for (int i = 0; i < 8; i += 2)
		{

			if (lineLineIntersection(p0, p1, quadSides[i + 0], quadSides[i + 1], &lineHit))
			{
				Vector2 pn = lineHit.getPosition();
				float t = lineHit.getTime();

				newVertexData.push_back(pn.x);
				newVertexData.push_back(pn.y);
				for (int j = 2; j < vertexSize; ++j)
				{
					float nv = vertexData[j] + (vertexData[j + vertexSize] - vertexData[j]) * t;
					newVertexData.push_back(nv);
				}

				verticesAdded++;
				if (verticesAdded == 2)
				{
					return newVertexData;
				}
			}
		}

		return newVertexData;
	}

	bool MathsUtils::pointInCircle(Vector2 const& point, Vector2 const& circleCentre, float circleRadius)
	{
		float distSq = point.distanceToSq(circleCentre);
		return distSq <= circleRadius * circleRadius;
	}

	bool MathsUtils::pointInArc(Vector2 const& point, Vector2 const& circleCentre, float circleRadius, float startAngle, float endAngle)
	{
		bool inCircle = pointInCircle(point, circleCentre, circleRadius);

		if (!inCircle)
		{
			return false;
		}

		float angle = wp::Vector2::UNIT_Y.clockwiseAngleTo(point - circleCentre);
		if (startAngle < endAngle)
		{
			return angle >= startAngle && angle <= endAngle;
		}
		else
		{
			return angle >= startAngle || angle <= endAngle;
		}
	}

	bool MathsUtils::pointInBox(Vector2 const& point, Vector2 const& boxMin, Vector2 const& boxMax)
	{
		return point.x >= boxMin.x && point.x <= boxMax.x && point.y >= boxMin.y && point.y <= boxMax.y;
	}

	bool MathsUtils::pointInTriangle(Vector2 const& point, Vector2 const& p0, Vector2 const& p1, Vector2 const& p2)
	{
		bool b1, b2, b3;

		b1 = sign(point, p0, p1) < 0.0f;
		b2 = sign(point, p1, p2) < 0.0f;
		b3 = sign(point, p2, p0) < 0.0f;

		return ((b1 == b2) && (b2 == b3));

		// This code fails for some very edge cases
		/*
		float denominator = ((p1.y - p2.y) * (p0.x - p2.x) + (p2.x - p1.x) * (p0.y - p2.y));
		float a = ((p1.y - p2.y) * (point.x - p2.x) + (p2.x - p1.x) * (point.y - p2.y)) / denominator;
		float b = ((p2.y - p0.y) * (point.x - p2.x) + (p0.x - p2.x) * (point.y - p2.y)) / denominator;
		float c = 1 - a - b;

		return 0 <= a && a <= 1 && 0 <= b && b <= 1 && 0 <= c && c <= 1;
		*/
	}

	bool MathsUtils::pointInConvexPolygon(Vector2 const& point, vector<Vector2> const& vertices)
	{
		for (uint32_t i = 1; i < vertices.size() - 1; ++i)
		{
			if (pointInTriangle(point, vertices[0], vertices[i], vertices[i + 1]))
			{
				return true;
			}
		}

		return false;
	}

	bool MathsUtils::pointInPolygon(Vector2 const& point, vector<Vector2> const& vertices)
	{
		bool oddNodes = false;
		auto numVertices = (uint32_t)vertices.size();

		for (uint32_t i = 0; i < numVertices; ++i)
		{
			uint32_t j = (i + 1) % numVertices;
			if (((vertices[i].y < point.y && vertices[j].y >= point.y) ||
			     (vertices[j].y < point.y && vertices[i].y >= point.y)) &&
			     (vertices[i].x <= point.x || vertices[j].x <= point.x))
			
			{
				float det = vertices[i].x + (point.y - vertices[i].y) / (vertices[j].y - vertices[i].y) * (vertices[j].x - vertices[i].x);
				if (det < point.x)
				{
					oddNodes = !oddNodes;
				}
			}
		}

		return oddNodes;
	}

	bool MathsUtils::boxIntersectsBox(Vector2 const& box0Min, Vector2 const& box0Max, Vector2 const& box1Min, Vector2 const& box1Max)
	{
		if (box0Min.x > box1Max.x)
		{
			return false;
		}
		if (box0Min.y > box1Max.y)
		{
			return false;
		}
		if (box0Max.x < box1Min.x)
		{
			return false;
		}
		if (box0Max.y < box1Min.y)
		{
			return false;
		}

		return true;
	}

	bool MathsUtils::boxIntersectsCircle(Vector2 const& boxMin, Vector2 const& boxMax, Vector2 const& circleCentre, float circleRadius)
	{
		Vector2 boxCentre = boxMin.lerp(boxMax, 0.5f);
		Vector2 boxRadius((boxMax.x - boxMin.x) * 0.5f, (boxMax.y - boxMin.y) * 0.5f);
		Vector2 distance(abs(circleCentre.x - boxCentre.x), abs(circleCentre.y - boxCentre.y));

		if (distance.x > boxRadius.x + circleRadius)
		{
			return false;
		}
		if (distance.y > boxRadius.y + circleRadius)
		{
			return false;
		}
		if (distance.x <= boxRadius.x)
		{
			return true;
		}
		if (distance.y <= boxRadius.y)
		{
			return true;
		}

		float cornerDistSq = distance.distanceToSq(boxRadius);
		return cornerDistSq <= circleRadius * circleRadius;
	}

	bool MathsUtils::circleIntersectsCircle(Vector2 const& circle0Centre, float circle0Radius, Vector2 const& circle1Centre, float circle1Radius)
	{
		float radii = circle0Radius + circle1Radius;
		return circle0Centre.distanceToSq(circle1Centre) < radii * radii;
	}

	MathsUtils::LineIntersectionType MathsUtils::lineIntersectsLine(Vector2 const& line0v0, Vector2 const& line0v1, Vector2 const& line1v0, Vector2 const& line1v1)
	{
		Vector2 s0 = line0v1 - line0v0;
		Vector2 s1 = line1v1 - line1v0;

		float det = s0.x * s1.y - s1.x * s0.y;

		// Are the lines parallel?
		if (fabs(det) < Epsilon)
		{
			// Are they coincident?
			float n0 = fabs(-s0.y * (line0v0.x - line1v0.x) + s0.x * (line0v0.y - line1v0.y));
			float n1 = fabs(s1.x * (line0v0.y - line1v0.y) - s1.y * (line0v0.x - line1v0.x));

			return (n0 < Epsilon && n1 < Epsilon) 
				? LineIntersectionType::Coincident
				: LineIntersectionType::NotIntersecting;
		}

		float u = (-s0.y * (line0v0.x - line1v0.x) + s0.x * (line0v0.y - line1v0.y)) / det;
		float v = (s1.x * (line0v0.y - line1v0.y) - s1.y * (line0v0.x - line1v0.x)) / det;

		return (u >= 0 && u <= 1 && v >= 0 && v <= 1) ? LineIntersectionType::Intersecting : LineIntersectionType::NotIntersecting;
	}

	MathsUtils::LineIntersectionType MathsUtils::lineIntersectsBox(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& boxMin, Vector2 const& boxMax)
	{
		// Check all four corners to see if they lie on the same side of the line.
		Side side1 = pointSideOnLine(boxMin.x, boxMin.y, linev0, linev1);
		Side side2 = pointSideOnLine(boxMax.x, boxMin.y, linev0, linev1);
		Side side3 = pointSideOnLine(boxMax.x, boxMax.y, linev0, linev1);
		Side side4 = pointSideOnLine(boxMin.x, boxMax.y, linev0, linev1);

		if (side1 == side2 && side1 == side3 && side1 == side4)
		{
			return LineIntersectionType::NotIntersecting;
		}

		// On different sides, check bounds
		if (linev0.x > boxMax.x && linev1.x > boxMax.x)
		{
			return LineIntersectionType::NotIntersecting;
		}
		if (linev0.x < boxMin.x && linev1.x < boxMin.x)
		{
			return LineIntersectionType::NotIntersecting;
		}
		if (linev0.y > boxMax.y && linev1.y > boxMax.y)
		{
			return LineIntersectionType::NotIntersecting;
		}
		if (linev0.y < boxMin.y && linev1.y < boxMin.y)
		{
			return LineIntersectionType::NotIntersecting;
		}

		if (pointInBox(linev0, boxMin, boxMax) && pointInBox(linev1, boxMin, boxMax))
		{
			return LineIntersectionType::Inside;
		}
		else
		{
			return LineIntersectionType::Intersecting;
		}
	}

	MathsUtils::LineIntersectionType MathsUtils::lineIntersectsCircle(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& circleCentre, float circleRadius)
	{
		float radiusSq = circleRadius * circleRadius;

		if (linev0.distanceToSq(circleCentre) < radiusSq &&
			linev1.distanceToSq(circleCentre) < radiusSq)
		{
			return LineIntersectionType::Inside;
		}

		// Find the closest point on the line to the circle
		Vector2 line = linev1 - linev0;
		Vector2 lineDir = line.normalisedCopy();

		Vector2 p = circleCentre - linev0;
		float proj = p.dot(lineDir);

		Vector2 closestPoint;
		if (proj <= 0)
		{
			closestPoint = linev0;
		}
		else if (proj >= line.length())
		{
			closestPoint = linev1;
		}
		else
		{
			closestPoint = linev0 + lineDir * proj;
		}

		// Check distance to centre
		return circleCentre.distanceToSq(closestPoint) < radiusSq 
			? LineIntersectionType::Intersecting
			: LineIntersectionType::NotIntersecting;
	}

	MathsUtils::LineIntersectionType MathsUtils::lineIntersectsTriangle(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& tv0, Vector2 const& tv1, Vector2 const& tv2)
	{
		// First check if the line intersects any of the triangle bounds
		LineIntersectionType res = lineIntersectsLine(tv0, tv1, linev0, linev1);
		if (res != LineIntersectionType::NotIntersecting)
		{
			return res;
		}

		res = lineIntersectsLine(tv1, tv2, linev0, linev1);
		if (res != LineIntersectionType::NotIntersecting)
		{
			return res;
		}

		res = lineIntersectsLine(tv2, tv0, linev0, linev1);
		if (res != LineIntersectionType::NotIntersecting)
		{
			return res;
		}

		// Then check if either line vertex is in the triangle
		int p1 = pointInTriangle(linev0, tv0, tv1, tv2) ? 1 : 0;
		int p2 = pointInTriangle(linev1, tv0, tv1, tv2) ? 1 : 0;
		int p3 = p1 + p2;

		if (p3 == 1)
		{
			return LineIntersectionType::Intersecting;
		}
		else if (p3 == 2)
		{
			return LineIntersectionType::Inside;
		}
		else
		{
			return LineIntersectionType::NotIntersecting;
		}
	}

	bool MathsUtils::boxIntersectsTriangle(Vector2 const& boxMin, Vector2 const& boxMax, Vector2 const& tv0, Vector2 const& tv1, Vector2 const& tv2)
	{
		// Check if any triangle points are in the box
		if (pointInBox(tv0, boxMin, boxMax))
		{
			return true;
		}
		if (pointInBox(tv1, boxMin, boxMax))
		{
			return true;
		}
		if (pointInBox(tv2, boxMin, boxMax))
		{
			return true;
		}

		// Do any triangle lines intersect the box?
		if (lineIntersectsBox(tv0, tv1, boxMin, boxMax) != LineIntersectionType::NotIntersecting)
		{
			return true;
		}
		if (lineIntersectsBox(tv1, tv2, boxMin, boxMax) != LineIntersectionType::NotIntersecting)
		{
			return true;
		}
		if (lineIntersectsBox(tv2, tv0, boxMin, boxMax) != LineIntersectionType::NotIntersecting)
		{
			return true;
		}

		// Final possibility is if the box is wholly inside the triangle
		return pointInTriangle(boxMin, tv0, tv1, tv2);
	}

	bool MathsUtils::circleIntersectsTriangle(Vector2 const& circleCentre, float circleRadius, Vector2 const& tv0, Vector2 const& tv1, Vector2 const& tv2)
	{
		// First check if the circle intersects any of the triangle bounds
		if (lineIntersectsCircle(tv0, tv1, circleCentre, circleRadius) != LineIntersectionType::NotIntersecting)
		{
			return true;
		}

		if (lineIntersectsCircle(tv1, tv2, circleCentre, circleRadius) != LineIntersectionType::NotIntersecting)
		{
			return true;
		}

		if (lineIntersectsCircle(tv2, tv0, circleCentre, circleRadius) != LineIntersectionType::NotIntersecting)
		{
			return true;
		}

		// Then check if the circle is in the triangle
		if (pointInTriangle(circleCentre, tv0, tv1, tv2))
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	bool MathsUtils::triangleIntersectsTriangle(Vector2 const& t0v0, Vector2 const& t0v1, Vector2 const& t0v2, Vector2 const& t1v0, Vector2 const& t1v1, Vector2 const& t1v2)
	{
		// See TriangleIntersection.h
		if (ORIENT_2D(t0v0, t0v1, t0v2) < 0.0f)
		{
			if (ORIENT_2D(t1v0, t1v1, t1v2) < 0.0f)
			{
				return ccw_tri_tri_intersection_2d(t0v0, t0v2, t0v1, t1v0, t1v2, t1v1);
			}
			else
			{
				return ccw_tri_tri_intersection_2d(t0v0, t0v2, t0v1, t1v0, t1v1, t1v2);
			}
		}
		else
		{
			if (ORIENT_2D(t1v0, t1v1, t1v2) < 0.0f)
			{
				return ccw_tri_tri_intersection_2d(t0v0, t0v1, t0v2, t1v0, t1v2, t1v1);
			}
			else
			{
				return ccw_tri_tri_intersection_2d(t0v0, t0v1, t0v2, t1v0, t1v1, t1v2);
			}
		}
	}

	bool MathsUtils::triangleIntersectsConvexPolygon(Vector2 const& tv0, Vector2 const& tv1, Vector2 const& tv2, vector<Vector2> const& polyvertices)
	{
		// This should be done using SAT, etc!  But this way is simple.
		// ...

		for (uint32_t i = 1; i < polyvertices.size() - 1; ++i)
		{
			if (triangleIntersectsTriangle(polyvertices[0], polyvertices[i], polyvertices[i + 1], tv0, tv1, tv2))
			{
				return true;
			}
		}

		return false;

	}

	bool MathsUtils::convexPolygonIntersectsConvexPolygon(vector<Vector2> const& poly0vertices, vector<Vector2> const& poly1vertices)
	{
		// This should be done using SAT, etc!  But this way is simple.
		// ...

		for (uint32_t i = 1; i < poly0vertices.size() - 1; ++i)
		{
			for (uint32_t j = 1; j < poly1vertices.size() - 1; ++j)
			{
				if (triangleIntersectsTriangle(
					poly0vertices[0], poly0vertices[i], poly0vertices[i + 1],
					poly1vertices[0], poly1vertices[j], poly1vertices[j + 1]))
				{
					return true;
				}
			}
		}

		return false;
	}
		
	MathsUtils::LineIntersectionType MathsUtils::rayRayIntersection(Vector2 const& ray0origin, Vector2 const& ray0dir, Vector2 const& ray1origin, Vector2 const& ray1dir, LineHit* hit)
	{
		float det = ray0dir.x * ray1dir.y - ray1dir.x * ray0dir.y;

		// Are the lines parallel?
		if (fabs(det) < Epsilon)
		{
			return LineIntersectionType::NotIntersecting;
		}

		float t = (ray1dir.x * (ray0origin.y - ray1origin.y) - ray1dir.y * (ray0origin.x - ray1origin.x)) / det;

		*hit = LineHit(t, ray0origin + ray0dir * t, ray1dir.normalisedCopy(), t == 0.0f || t == 1.0f);
		return LineIntersectionType::Intersecting;
	}

	MathsUtils::LineIntersectionType MathsUtils::lineLineIntersection(Vector2 const& line0v0, Vector2 const& line0v1, Vector2 const& line1v0, Vector2 const& line1v1, LineHit* hit)
	{
		Vector2 s0 = line0v1 - line0v0;
		Vector2 s1 = line1v1 - line1v0;

		float det = s0.x * s1.y - s1.x * s0.y;

		// Are the lines parallel?
		if (fabs(det) < Epsilon)
		{
			return LineIntersectionType::NotIntersecting;
		}

		float u = (-s0.y * (line0v0.x - line1v0.x) + s0.x * (line0v0.y - line1v0.y)) / det;
		float v = ( s1.x * (line0v0.y - line1v0.y) - s1.y * (line0v0.x - line1v0.x)) / det;

		if (u >= 0 && u <= 1 && v >= 0 && v <= 1)
		{
			bool touching = u == 0.0f || u == 1.0f || v == 0.0f || v == 1.0f;

			if (hit)
			{
				// If the first line intersects the other line with that line's points "wound clockwise"
				// then consider this to be "entering" (on the assume that the line is part of an anticlockwise-wound
				// polygon).
				uint32_t flags = det < 0 ? LineHit::Flags::HitEnters : LineHit::Flags::HitExits;
				*hit = LineHit(v, line0v0.lerp(line0v1, v), (line1v0 - line1v1).perpendicular().normalisedCopy(), touching, flags);
			}

			return LineIntersectionType::Intersecting;
		}

		return LineIntersectionType::NotIntersecting;
	}

	MathsUtils::LineIntersectionType MathsUtils::lineBoxIntersection(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& boxMin, Vector2 const& boxMax, LineHit* hit1, LineHit* hit2)
	{
		Vector2 delta = linev1 - linev0;
		Vector2 scale(1 / delta.x, 1 / delta.y);
		Vector2 half = (boxMax - boxMin) / 2.0f;
		Vector2 pos = boxMin + half;

		Vector2 sign(scale.x < 0 ? -1.0f : 1.0f, scale.y < 0 ? -1.0f : 1.0f);
		Vector2 nearTime = (pos - sign * half - linev0) * scale;
		Vector2 farTime = (pos + sign * half - linev0) * scale;

		if (nearTime.x > farTime.y || nearTime.y > farTime.x)
		{
			return LineIntersectionType::NotIntersecting;
		}

		float nearTimeValue = max(nearTime.x, nearTime.y);
		float farTimeValue = min(farTime.x, farTime.y);

		if (nearTimeValue >= 1 || farTimeValue <= 0)
		{
			return LineIntersectionType::NotIntersecting;
		}

		if (nearTimeValue <= 0.0f && farTimeValue >= 1.0f)
		{
			return LineIntersectionType::Inside;
		}

		// If both hit result pointers are null, then just return that we have intersecting,
		// without calculating anything.
		if (!hit1 && !hit2)
		{
			return LineIntersectionType::Intersecting;
		}

		// What kind of intersection was it?
		uint32_t hitFlags = LineHit::Flags::None;
		if (nearTimeValue >= 0)
		{
			hitFlags |= LineHit::Flags::HitEnters;
		}
		if (farTimeValue <= 1.0f)
		{
			hitFlags |= LineHit::Flags::HitExits;
		}

		auto hitResult = hitFlags == (LineHit::Flags::HitEnters | LineHit::Flags::HitExits)
			? LineIntersectionType::DoublyIntersecting
			: LineIntersectionType::Intersecting;

		if (hit1)
		{
			// Calculate hit time
			float hitTime;
			Vector2 hitNormal(0, 0);
			if (hitFlags & LineHit::Flags::HitEnters)
			{
				hitTime = nearTimeValue;
				if (nearTime.x > nearTime.y)
				{
					hitNormal.x = -sign.x;
				}
				else
				{
					hitNormal.y = -sign.y;
				}
			}
			else
			{
				hitTime = farTimeValue;
				if (farTime.y > farTime.x)
				{
					hitNormal.x = -sign.x;
				}
				else
				{
					hitNormal.y = -sign.y;
				}
			}
				
			// Calculate hit position
			Vector2 hitPosition = linev0 + delta * hitTime;
			*hit1 = LineHit(hitTime, hitPosition, hitNormal, hitTime == 0.0f || hitTime == 1.0f, hitFlags);
		}
		if (hit2)
		{
			if (hitResult == LineIntersectionType::DoublyIntersecting)
			{
				// Calculate hit time
				float hitTime = farTimeValue;

				// Calculate hit normal
				Vector2 hitNormal = Vector2::ZERO;
				if (farTime.x > farTime.y)
				{
					hitNormal.x = sign.x;
				}
				else
				{
					hitNormal.y = sign.y;
				}

				// Calculate hit position
				Vector2 hitPosition = linev0 + delta * hitTime;
				*hit2 = LineHit(hitTime, hitPosition, hitNormal, hitTime == 0.0f || hitTime == 1.0f, hitFlags);
			}
		}
			
		return hitResult;
	}

	MathsUtils::LineIntersectionType MathsUtils::lineCircleIntersection(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& circleCentre, float circleRadius, LineHit* hit1, LineHit* hit2)
	{
		Vector2 d = linev1 - linev0;
		Vector2 f = linev0 - circleCentre;

		float a = d.dot(d);
		float b = 2 * f.dot(d);
		float c = f.dot(f) - circleRadius * circleRadius;

		float discriminant = b * b - 4 * a * c;
		if (discriminant < 0)
		{
			return LineIntersectionType::NotIntersecting;
		}

		discriminant = sqrt(discriminant);

		float t1 = (-b - discriminant) / (2 * a);
		float t2 = (-b + discriminant) / (2 * a);

		if (t1 < 0 && t2 > 1)
		{
			return LineIntersectionType::Inside;
		}

		bool p1 = t1 >= 0 && t1 <= 1;
		bool p2 = t2 >= 0 && t2 <= 1;

		if (!p1 && !p2)
		{
			return LineIntersectionType::NotIntersecting;
		}

		// If both hit result pointers are null, then just return that we have intersecting,
		// without calculating anything.
		if (!hit1 && !hit2)
		{
			return LineIntersectionType::Intersecting;
		}

		// What kind of intersection was it?
		uint32_t hitFlags = LineHit::Flags::None;

		if (p1)
		{
			hitFlags |= LineHit::Flags::HitEnters;
		}
		if (p2)
		{
			hitFlags |= LineHit::Flags::HitExits;
		}

		auto hitResult = hitFlags == (LineHit::Flags::HitEnters | LineHit::Flags::HitExits)
			? LineIntersectionType::DoublyIntersecting
			: LineIntersectionType::Intersecting;

		if (hit1)
		{
			float hitTime = p1 ? t1 : t2;
			uint32_t flags = p1 ? LineHit::Flags::HitEnters : LineHit::Flags::HitExits;

			Vector2 hitPosition = linev0.lerp(linev1, hitTime);
			Vector2 hitNormal = (hitPosition - circleCentre).normalisedCopy();

			if (flags == LineHit::Flags::HitExits)
			{
				hitNormal = -hitNormal;
			}

			*hit1 = LineHit(hitTime, hitPosition, hitNormal, hitTime == 0.0f || hitTime == 1.0f, flags);
		}
		if (hit2)
		{
			if (p1 && p2)
			{
				Vector2 hitPosition = linev0.lerp(linev1, t2);
				Vector2 hitNormal = (hitPosition - circleCentre).normalisedCopy();

				*hit2 = LineHit(t2, hitPosition, hitNormal, t2 == 0.0f || t2 == 1.0f, LineHit::Flags::HitExits);
			}
		}

		return hitResult;
	}

	MathsUtils::LineIntersectionType MathsUtils::lineArcIntersection(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& circleCentre, float circleRadius, float startAngle, float endAngle, LineHit* hit1, LineHit* hit2)
	{
		LineHit h1, h2;
		auto isect = lineCircleIntersection(linev0, linev1, circleCentre, circleRadius, &h1, &h2);

		if (hit1)
		{
			*hit1 = h1;
		}
		if (hit2)
		{
			*hit2 = h2;
		}

		// If it doesn't intersect circle, it won't intersect the arc
		if (isect == LineIntersectionType::NotIntersecting)
		{
			return isect;
		}

		// Get intersection points and see if they fall on the arc
		if (isect == LineIntersectionType::Intersecting)
		{
			auto const& p = h1.getPosition();
			float angle = wp::Vector2::UNIT_Y.clockwiseAngleTo(p - circleCentre);

			if (startAngle < endAngle)
			{
				return (angle >= startAngle && angle <= endAngle) ? isect : LineIntersectionType::NotIntersecting;
			}
			else
			{
				return (angle <= startAngle || angle >= endAngle) ? isect : LineIntersectionType::NotIntersecting;
			}
		}
		else if (isect == LineIntersectionType::DoublyIntersecting)
		{
			// Determine if neither, either or both are intersecting
			auto const& p1 = h1.getPosition();
			auto const& p2 = h2.getPosition();
			float angle1 = wp::Vector2::UNIT_Y.clockwiseAngleTo(p1 - circleCentre);
			float angle2 = wp::Vector2::UNIT_Y.clockwiseAngleTo(p2 - circleCentre);

			bool a1, a2;
			if (startAngle < endAngle)
			{
				//a1 = angle1 >= startAngle && angle1 <= endAngle;
				//a2 = angle2 >= startAngle && angle2 <= endAngle;
				a1 = GTE_EPSILON(angle1, startAngle) && LTE_EPSILON(angle1, endAngle);
				a2 = GTE_EPSILON(angle2, startAngle) && LTE_EPSILON(angle2, endAngle);
			}
			else
			{
				//a1 = angle1 <= startAngle || angle1 >= endAngle;
				//a2 = angle2 <= startAngle || angle2 >= endAngle;
				a1 = LTE_EPSILON(angle1, startAngle) || GTE_EPSILON(angle1, endAngle);
				a2 = LTE_EPSILON(angle2, startAngle) || GTE_EPSILON(angle2, endAngle);
			}

			if (a1)
			{
				if (a2)
				{
					return LineIntersectionType::DoublyIntersecting;
				}
				else
				{
					return LineIntersectionType::Intersecting;
				}
			}
			else if (a2 && !a1)
			{
				if (hit1)
				{
					*hit1 = h2;
				}

				return LineIntersectionType::Intersecting;
			}
		}

		return LineIntersectionType::NotIntersecting;
	}

	MathsUtils::LineIntersectionType MathsUtils::lineTriangleIntersection(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& tv0, Vector2 const& tv1, Vector2 const& tv2, Vector2* intersect)
	{
		// The line will intersect at most 2 edges of the triangle, so check each case
		// individually, for speed.
		LineHit intersect1, intersect2, intersect3;
		auto res1 = lineLineIntersection(linev0, linev1, tv0, tv1, &intersect1);
		auto res2 = lineLineIntersection(linev0, linev1, tv1, tv2, &intersect2);

		// Does the line intersect triangle edges 1 and 2?
		if (res1 == LineIntersectionType::Intersecting && res2 == LineIntersectionType::Intersecting)
		{
			if (intersect)
			{
				*intersect = linev0.lerp(linev1, min(intersect1.getTime(), intersect2.getTime()));
			}

			return LineIntersectionType::Intersecting;
		}
			
		auto res3 = lineLineIntersection(linev0, linev1, tv2, tv0, &intersect3);

		// Does the line intersect triangle edges 1 and 3?
		if (res1 == LineIntersectionType::Intersecting && res3 == LineIntersectionType::Intersecting)
		{
			if (intersect)
			{
				*intersect = linev0.lerp(linev1, min(intersect1.getTime(), intersect3.getTime()));
			}

			return LineIntersectionType::Intersecting;
		}

		// Does the line intersect triangle edges 2 and 3?
		if (res2 == LineIntersectionType::Intersecting && res3 == LineIntersectionType::Intersecting)
		{
			if (intersect)
			{
				*intersect = linev0.lerp(linev1, min(intersect2.getTime(), intersect3.getTime()));
			}

			return LineIntersectionType::Intersecting;
		}

		// If we get here, it is not intersecting any edges, and is either inside or outside.
		if (pointInTriangle(linev0, tv0, tv1, tv2) && pointInTriangle(linev1, tv0, tv1, tv2))
		{
			return LineIntersectionType::Inside;
		}
		else
		{
			return LineIntersectionType::NotIntersecting;
		}
	}

	void MathsUtils::rotatePoint(Vector2& point, Vector2 const& origin, float angle)
	{
		float angleInRadians = WP_DEGTORAD(angle);
		float sinAngle = sin(angleInRadians);
		float cosAngle = cos(angleInRadians);

		point -= origin;

		// cos -sin
		// sin cos
		float newX = point.x * cosAngle - point.y * sinAngle;
		float newY = point.x * sinAngle + point.y * cosAngle;

		point.set(newX + origin.x, newY + origin.y);
	}

	void MathsUtils::rotatePoint(Vector2& point, float angle)
	{
		rotatePoint(point, Vector2::ZERO, angle);
	}

	void MathsUtils::rotatePoints(vector<Vector2>& points, Vector2 const& origin, float angle)
	{
		float angleInRadians = WP_DEGTORAD(angle);
		float sinAngle = sin(angleInRadians);
		float cosAngle = cos(angleInRadians);

		for (auto& point: points)
		{
			point -= origin;

			// cos -sin
			// sin cos
			float newX = point.x * cosAngle - point.y * sinAngle;
			float newY = point.x * sinAngle + point.y * cosAngle;

			point.set(newX + origin.x, newY + origin.y);
		}
	}

	void MathsUtils::rotatePoints(vector<Vector2>& points, float angle)
	{
		rotatePoints(points, Vector2::ZERO, angle);
	}

	bool MathsUtils::sweepCircleAgainstLine(Vector2 const& circleCentre, float circleRadius, Vector2 const& circleTarget, Vector2 const& linev0, Vector2 const& linev1, float* t)
	{
		// Intersect against line projected towards circle.  This is the line extruded towards the circle.
		Side lineSide = pointSideOnLine(circleCentre, linev0, linev1);
		Vector2 lineNormal = (linev1 - linev0).perpendicular().normalisedCopy();

		if (lineSide == Side::Right)
		{
			lineNormal *= -1;
		}

		Vector2 projectedV0 = linev0 + lineNormal * circleRadius;
		Vector2 projectedV1 = linev1 + lineNormal * circleRadius;

		LineHit hit;
		auto hitType = lineLineIntersection(circleCentre, circleTarget, projectedV0, projectedV1, &hit);
		if (hitType != LineIntersectionType::NotIntersecting)
		{
			*t = hit.getTime();
			return true;
		}

		// If it hasn't directly intersected the line, it may still 'graze' one of the endpoints as it passes by.  Test the intersection of the line 
		// against two circles representing its inflated endpoints.
		lineCircleIntersection(circleCentre, circleTarget, linev0, circleRadius, &hit);

		LineHit hit2;
		lineCircleIntersection(circleCentre, circleTarget, linev1, circleRadius, &hit2);

		// If at least one of them has hit, then at least one 't' value
		if (hit.getFlags() != LineHit::Flags::None || hit2.getFlags() != LineHit::Flags::None)
		{
			float t1 = hit.getTime();
			float t2 = hit2.getTime();

			// Take the smallest 't' value above zero (ie which has actually hit).
			Vector2 circleMove = circleTarget - circleCentre;
			*t = (((t1 >= 0 && t2 >= 0) ? min(t1, t2) : max(t1, t2)));
			return true;
		}

		// If we get here, there's no intersection.
		*t = 1.0f;
		return false;
	}

	bool MathsUtils::sweepAABBAgainstLine(Vector2 const& aabbCentre, Vector2 const& halfSize, Vector2 const& aabbTarget, Vector2 const& linev0, Vector2 const& linev1, float* t)
	{
		Side lineSide = pointSideOnLine(aabbCentre, linev0, linev1);
		Vector2 lineNormal = (linev1 - linev0).perpendicular().normalisedCopy();

		if (lineSide == Side::Right)
		{
			lineNormal *= -1;
		}

		LineHit hit;
		if (linev0.x == linev1.x || linev0.y == linev1.y)
		{
			// Straight line: expand (linev0, linev1) by halfSize
			Vector2 boxMin(min(linev0.x, linev1.x) - halfSize.x, min(linev0.y, linev1.y) - halfSize.y);
			Vector2 boxMax(max(linev0.x, linev1.x) + halfSize.x, max(linev0.y, linev1.y) + halfSize.y);
			
			if (lineBoxIntersection(aabbCentre, aabbTarget, boxMin, boxMax, &hit) != MathsUtils::LineIntersectionType::NotIntersecting)
			{
				*t = hit.getTime();
				return true;
			}
		}

		// If the line cannot be inflated to an AABB, then get the Minkowski sum, and check the intersections against its border.  Get the 
		// end, near and far vertices.
		Vector2 ev0, ev1, nv0, nv1, fv0, fv1;
		getLineSweepHull(linev0, linev1, aabbCentre, halfSize, ev0, ev1, nv0, nv1, fv0, fv1);
		
		// Intersect with the near line, and the two near sides.  If we are on the same side as the far sides, intersect those too.
		if (lineLineIntersection(aabbCentre, aabbTarget, nv0, nv1, &hit) != LineIntersectionType::NotIntersecting)
		{
			*t = hit.getTime();
			return true;
		}
		if (lineLineIntersection(aabbCentre, aabbTarget, ev0, nv0, &hit) != LineIntersectionType::NotIntersecting)
		{
			*t = hit.getTime();
			return true;
		}
		if (lineLineIntersection(aabbCentre, aabbTarget, nv1, ev1, &hit) != LineIntersectionType::NotIntersecting)
		{
			*t = hit.getTime();
			return true;
		}
		if (pointSideOnLine(aabbCentre, ev1, fv1) == Side::Left)
		{
			if (lineLineIntersection(aabbCentre, aabbTarget, ev1, fv1, &hit) != LineIntersectionType::NotIntersecting)
			{
				*t = hit.getTime();
				return true;
			}
		}
		if (pointSideOnLine(aabbCentre, fv0, ev0) == Side::Left)
		{
			if (lineLineIntersection(aabbCentre, aabbTarget, fv0, ev0, &hit) != LineIntersectionType::NotIntersecting)
			{
				*t = hit.getTime();
				return true;
			}
		}

		// If we get here, there's no intersection.
		*t = 1.0f;
		return false;
	}

} // WP_NAMESPACE
