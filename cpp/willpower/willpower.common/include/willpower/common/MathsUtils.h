#pragma once

#include <string>
#include <vector>

#include "willpower/common/Platform.h"
#include "willpower/common/Globals.h"
#include "willpower/common/Vector2.h"
#include "willpower/common/LineHit.h"
#include "willpower/common/BoundingBox.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API MathsUtils
	{
	public:

		enum Side
		{
			Right,
			Left,
			On
		};

		enum LineIntersectionType
		{
			NotIntersecting,
			Touching,
			Coincident,
			Inside,
			Intersecting,
			DoublyIntersecting,
		};

	public:

		static float Epsilon;

	private:

		inline static float xIntersect(Vector2 const& line0v0, Vector2 const& line0v1, Vector2 const& line1v0, Vector2 const& line1v1)
		{
			float num = (line0v0.x * line0v1.y - line0v0.y * line0v1.x) * (line1v0.x - line1v1.x) - (line0v0.x - line0v1.x) * (line1v0.x * line1v1.y - line1v0.y * line1v1.x);
			float den = (line0v0.x - line0v1.x) * (line1v0.y - line1v1.y) - (line0v0.y - line0v1.y) * (line1v0.x - line1v1.x);
			return num / den;
		}

		inline static float yIntersect(Vector2 const& line0v0, Vector2 const& line0v1, Vector2 const& line1v0, Vector2 const& line1v1)
		{
			float num = (line0v0.x * line0v1.y - line0v0.y * line0v1.x) * (line1v0.y - line1v1.y) - (line0v0.y - line0v1.y) * (line1v0.x * line1v1.y - line1v0.y * line1v1.x);
			float den = (line0v0.x - line0v1.x) * (line1v0.y - line1v1.y) - (line0v0.y - line0v1.y) * (line1v0.x - line1v1.x);
			return num / den;
		}

	public:

		static int modulo(int x, int n);

		inline static float sign(Vector2 p1, Vector2 p2, Vector2 p3)
		{
			return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
		}

		inline static int valueSign(float value)
		{
			return (value > 0) - (value < 0);
		}

		static int nextPow2(int v)
		{
			v--;

			v |= v >> 1;
			v |= v >> 2;
			v |= v >> 4;
			v |= v >> 8;
			v |= v >> 16;

			v++;

			return v;
		}

		static int bitsRequired(int v)
		{
			if (!v)
			{
				return 0;
			}

			int ret = 1;

			while (v >>= 1)
				ret++;

			return ret;
		}

		//
		// Utility
		//
		static float degrees(float radians);

		static float radians(float degrees);

		// Get the side of a line that a point lies on.
		static Side pointSideOnLine(float x, float y, Vector2 const& linev0, Vector2 const& linev1);

		static Side pointSideOnLine(Vector2 const& point, Vector2 const& linev0, Vector2 const& linev1);

		static Winding pointsWinding(std::vector<Vector2> const& points);

		static bool pointsFormLine(Vector2 const& point1, Vector2 const& point2, Vector2 const& point3);

		static bool pointsFormLine(std::vector<Vector2> const& points, int offset);

		static void getLineSweepHull(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& point, Vector2 const& halfSize, Vector2& ev0, Vector2& ev1, Vector2& nv0, Vector2& nv1, Vector2& fv0, Vector2& fv1);

		static float anticlockwiseAngleBetween(Vector2 const& v0, Vector2 const& v1, Vector2 const& v2);

		static float clockwiseAngleBetween(Vector2 const& v0, Vector2 const& v1, Vector2 const& v2);

		//
		// Geometry queries
		//
		static bool polygonIsConvex(std::vector<Vector2> const& vertices);

		static float convexPolygonArea(std::vector<Vector2> const& vertices);

		static void barycentricCoords(Vector2 const& p, Vector2 const& v0, Vector2 const& v1, Vector2 const& v2, float& u, float& v, float& w);

		static BoundingBox getPolygonBounds(std::vector<Vector2> const& vertices);

		static float triangleArea(Vector2 const& p0, Vector2 const& p1, Vector2 const& p2);

		static std::vector<Vector2> clipPolygonAgainstPolygon(std::vector<Vector2> const& points, std::vector<Vector2> const& clipper);

		static std::vector<float> clipPolygonVertexDataAgainstPolygon(std::vector<float> const& vertexData, int vertexSize, std::vector<Vector2> const& clipper);

		static std::vector<Vector2> clipLineAgainstQuad(Vector2 const& v0, Vector2 const& v1, Vector2 const& q0, Vector2 const& q1);

		static std::vector<float> clipLineVertexDataAgainstQuad(std::vector<float> const& vertexData, int vertexSize, Vector2 const& q0, Vector2 const& q1);
		
		//
		// Intersection queries, no intersection points calculated.
		//
		static bool pointOnLine(Vector2 const& point, Vector2 const& linev0, Vector2 const& linev1, float maxDistance);

		static bool pointInCircle(Vector2 const& point, Vector2 const& circleCentre, float circleRadius);

		static bool pointInArc(Vector2 const& point, Vector2 const& circleCentre, float circleRadius, float startAngle, float endAngle);

		static bool pointInBox(Vector2 const& point, Vector2 const& boxMin, Vector2 const& boxMax);

		static bool pointInTriangle(Vector2 const& point, Vector2 const& p0, Vector2 const& p1, Vector2 const& p2);
			
		static bool pointInConvexPolygon(Vector2 const& point, std::vector<Vector2> const& vertices);

		static bool pointInPolygon(Vector2 const& point, std::vector<Vector2> const& vertices);

		static bool boxIntersectsBox(Vector2 const& box0Min, Vector2 const& box0Max, Vector2 const& box1Min, Vector2 const& box1Max);

		static bool boxIntersectsCircle(Vector2 const& boxMin, Vector2 const& boxMax, Vector2 const& circleCentre, float circleRadius);

		static bool circleIntersectsCircle(Vector2 const& circle0Centre, float circle0Radius, Vector2 const& circle1Centre, float circle1Radius);

		static LineIntersectionType lineIntersectsLine(Vector2 const& line0v0, Vector2 const& line0v1, Vector2 const& line1v0, Vector2 const& line1v1);

		static LineIntersectionType lineIntersectsBox(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& boxMin, Vector2 const& boxMax);

		static LineIntersectionType lineIntersectsCircle(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& circleCentre, float circleRadius);

		static LineIntersectionType lineIntersectsTriangle(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& tv0, Vector2 const& tv1, Vector2 const& tv2);

		static bool boxIntersectsTriangle(Vector2 const& boxMin, Vector2 const& boxMax, Vector2 const& tv0, Vector2 const& tv1, Vector2 const& tv2);

		static bool circleIntersectsTriangle(Vector2 const& circleCentre, float circleRadius, Vector2 const& tv0, Vector2 const& tv1, Vector2 const& tv2);
			
		static bool triangleIntersectsTriangle(Vector2 const& t0v0, Vector2 const& t0v1, Vector2 const& t0v2, Vector2 const& t1v0, Vector2 const& t1v1, Vector2 const& t1v2);

		static bool triangleIntersectsConvexPolygon(Vector2 const& tv0, Vector2 const& tv1, Vector2 const& tv2, std::vector<Vector2> const& polyvertices);

		static bool convexPolygonIntersectsConvexPolygon(std::vector<Vector2> const& poly0vertices, std::vector<Vector2> const& poly1vertices);

		//
		// Intersection queries, with closest intersection point calculated.
		//
		static LineIntersectionType rayRayIntersection(Vector2 const& ray0origin, Vector2 const& ray0dir, Vector2 const& ray1origin, Vector2 const& ray1dir, LineHit* hit = nullptr);

		static LineIntersectionType lineLineIntersection(Vector2 const& line0v0, Vector2 const& line0v1, Vector2 const& line1v0, Vector2 const& line1v1, LineHit* hit = nullptr);

		static LineIntersectionType lineBoxIntersection(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& boxMin, Vector2 const& boxMax, LineHit* hit1 = nullptr, LineHit* hit2 = nullptr);

		static LineIntersectionType lineCircleIntersection(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& circleCentre, float circleRadius, LineHit* hit1 = nullptr, LineHit* hit2 = nullptr);

		static LineIntersectionType lineArcIntersection(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& circleCentre, float circleRadius, float startAngle, float endAngle, LineHit* hit1 = nullptr, LineHit* hit2 = nullptr);

		static LineIntersectionType lineTriangleIntersection(Vector2 const& linev0, Vector2 const& linev1, Vector2 const& tv0, Vector2 const& tv1, Vector2 const& tv2, Vector2* intersect);

		//
		// Point manipulation
		//
		static void rotatePoint(Vector2& point, Vector2 const& origin, float angle);

		static void rotatePoint(Vector2& point, float angle);

		static void rotatePoints(std::vector<Vector2>& points, Vector2 const& origin, float angle);

		static void rotatePoints(std::vector<Vector2>& points, float angle);

		//
		// Sweep tests
		//
		static bool sweepCircleAgainstLine(Vector2 const& circleCentre, float circleRadius, Vector2 const& circleTarget, Vector2 const& linev0, Vector2 const& linev1, float* t);

		static bool sweepAABBAgainstLine(Vector2 const& aabbCentre, Vector2 const& halfSize, Vector2 const& aabbTarget, Vector2 const& linev0, Vector2 const& linev1, float* t);
	};

} // WP_NAMESPACE
