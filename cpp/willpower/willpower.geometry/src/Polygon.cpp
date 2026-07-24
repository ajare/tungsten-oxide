#include <map>
#include <cassert>
#include <algorithm>

#include <mapbox/earcut.hpp>

#include <willpower/common/StringUtils.h>

#include "willpower/common/MathsUtils.h"
#include "willpower/common/WillpowerWalker.h"

#include "willpower/geometry/Polygon.h"
#include "willpower/geometry/Mesh.h"
#include "willpower/geometry/MeshUtils.h"
#include "willpower/geometry/Exception.h"

#undef max

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace std;
		using namespace utils;

		void Polygon::Triangulation::copyFrom(Triangulation const& other)
		{
			mIndices = other.mIndices;
		}

		Polygon::Triangulation::Triangulation()
		{
		}

		Polygon::Triangulation::Triangulation(Triangulation const& other)
		{
			copyFrom(other);
		}

		Polygon::Triangulation& Polygon::Triangulation::operator=(Triangulation const& other)
		{
			copyFrom(other);
			return *this;
		}

		void Polygon::Triangulation::getVertexIndices(size_t triangleIndex, uint32_t& v0, uint32_t& v1, uint32_t& v2) const
		{
			v0 = mIndices[triangleIndex * 3 + 0];
			v1 = mIndices[triangleIndex * 3 + 1];
			v2 = mIndices[triangleIndex * 3 + 2];
		}

		size_t Polygon::Triangulation::getNumTriangles() const
		{
			return mIndices.size() / 3;
		}

		vector<Polygon::Triangulation::Point> Polygon::Triangulation::preprocessLoop(IndexVector const& vertIndices, Mesh const* mesh, map<uint32_t, uint32_t>& mapping, uint32_t options)
		{
			vector<Point> points;

			float epsilonSq = MathsUtils::Epsilon;

			auto numVertices = vertIndices.size();
			Vector2 const& vertex0 = mesh->getVertex(vertIndices[0]).getPosition();
			for (uint32_t i = 0; i < numVertices; ++i)
			{
				auto vIndex = vertIndices[i];
				Vector2 const& vertex = mesh->getVertex(vIndex).getPosition();

				// Don't allow multiple vertices too close to each other as this can crash the triangulator.
				if ((options & Options::NoCospatialVertices) && i > 0)
				{
					Vector2 const& vertexl = mesh->getVertex(vertIndices[i - 1]).getPosition();
					if (vertex.distanceToSq(vertexl) < epsilonSq)
					{
						continue;
					}

					// Check last point against first
					if (i == vertIndices.size() - 1)
					{
						if (vertex.distanceToSq(vertex0) < epsilonSq)
						{
							continue;
						}
					}
				}

				// Don't allow collinear points, ie three or more on a straight line.
				if ((options & Options::NoCollinearVertices) && i > 0)
				{
					auto nextIndex = (i + 1) % numVertices;
					Vector2 const& vertexl = mesh->getVertex(vertIndices[i - 1]).getPosition();
					Vector2 const& vertexn = mesh->getVertex(vertIndices[nextIndex]).getPosition();
					if (MathsUtils::pointsFormLine(vertexl, vertex, vertexn))
					{
						continue;
					}
				}

				auto mapcount = (uint32_t)mapping.size();
				mapping[mapcount] = vIndex;
				points.push_back({ vertex.x, vertex.y });
			}

			return points;
		}

		bool Polygon::Triangulation::build(Polygon const& polygon, Mesh const* mesh, uint32_t options)
		{
			mIndices.clear();

			auto vertIndices = polygon.getOrderedVertexIndices();

			if (vertIndices.size() < 3)
			{
				return false;
			}

			// TODO: need to map each incoming vertex to its index in the final vertex list
			map<uint32_t, uint32_t> mapping;

			// TODO: do we need to preprocess with earcut lib?

			// Border
			auto border = preprocessLoop(vertIndices, mesh, mapping, options);

			vector<vector<Point>> triangulee;
			if (border.size() < 3)
			{
				return false;
			}

			triangulee.push_back(border);

			// Holes
			auto holeIndices = polygon.getHoleIndices();
			for (auto holeIndex: holeIndices)
			{
				auto const& hole = mesh->getPolygon(holeIndex);
				auto holeVertIndices = hole.getOrderedVertexIndices();

				auto holeBorder = preprocessLoop(holeVertIndices, mesh, mapping, options);
				triangulee.push_back(holeBorder);
			}

			// Triangulate
			auto indices = mapbox::earcut<uint32_t>(triangulee);

			// Go through indices and map them to the actual vertex indices of the mesh
			for (auto index: indices)
			{
				mIndices.push_back(mapping[index]);
			}

			return true;
		}

		bool Polygon::Triangulation::pointInside(float x, float y, Mesh const* mesh) const
		{
			Vector2 point(x, y);
			for (size_t i = 0; i < mIndices.size(); i += 3)
			{
				Vector2 v0 = mesh->getVertex(mIndices[i + 0]).getPosition();
				Vector2 v1 = mesh->getVertex(mIndices[i + 1]).getPosition();
				Vector2 v2 = mesh->getVertex(mIndices[i + 2]).getPosition();

				if (MathsUtils::pointInTriangle(point, v0, v1, v2))
				{
					return true;
				}
			}

			return false;
		}

		bool Polygon::Triangulation::intersects(Triangulation const& other, Mesh const* mesh) const
		{
			for (size_t i = 0; i < mIndices.size(); i += 3)
			{
				Vector2 tv0 = mesh->getVertex(mIndices[i + 0]).getPosition();
				Vector2 tv1 = mesh->getVertex(mIndices[i + 1]).getPosition();
				Vector2 tv2 = mesh->getVertex(mIndices[i + 2]).getPosition();

				for (size_t j = 0; j < other.mIndices.size(); j += 3)
				{
					Vector2 ov0 = mesh->getVertex(other.mIndices[j + 0]).getPosition();
					Vector2 ov1 = mesh->getVertex(other.mIndices[j + 1]).getPosition();
					Vector2 ov2 = mesh->getVertex(other.mIndices[j + 2]).getPosition();

					if (MathsUtils::triangleIntersectsTriangle(tv0, tv1, tv2, ov0, ov1, ov2))
					{
						return true;
					}
				}
			}

			return false;
		}

		wp::Triangulation Polygon::Triangulation::createBasicTriangulation(Mesh const* mesh) const
		{
			wp::Triangulation triangulation;

			for (size_t i = 0; i < getNumTriangles(); ++i)
			{
				uint32_t i0, i1, i2;
				getVertexIndices(i, i0, i1, i2);

				triangulation.addTriangle(
					mesh->getVertex(i0).getPosition(),
					mesh->getVertex(i1).getPosition(),
					mesh->getVertex(i2).getPosition()
				);
			}

			return triangulation;
		}

		Polygon::Polygon(IndexVector const& edgeData)
			: DirectedEdgeLoop(Winding::Anticlockwise, edgeData)
			, mPublicId(-1)
			, mAttributeIndex(-1)
			, mTriangleDataCached(true)
		{
		}

		Polygon::Polygon(Polygon const& other)
			: DirectedEdgeLoop(other)
		{
			copyFrom(other);
		}

		Polygon& Polygon::operator=(Polygon const& other)
		{
			DirectedEdgeLoop::operator=(other);
			copyFrom(other);
			return *this;
		}

		bool Polygon::operator==(Polygon const& other) const
		{
			return mPublicId == other.mPublicId;
		}

		bool Polygon::operator!=(Polygon const& other) const
		{
			return !(*this == other);
		}

		void Polygon::copyFrom(Polygon const& other)
		{
			mPublicId = other.mPublicId;
			mAttributeIndex = other.mAttributeIndex;
			mHoleIndices = other.mHoleIndices;

			// Copy triangle data cache
			mTriangleDataCached = other.mTriangleDataCached;
			mTriangleData = other.mTriangleData;
		}

		int32_t Polygon::getPublicId() const
		{
			return mPublicId;
		}

		Polygon::Triangulation const& Polygon::getTriangulation() const
		{
			if (!mTriangleDataCached)
			{
				cacheTriangleData();
				mTriangleDataCached = true;
			}

			return mTriangleData;
		}

		void Polygon::cacheTriangleData() const
		{
			mTriangleData.build(*this, mwMesh);
		}

		void Polygon::invalidateTriangleData()
		{
			mTriangleDataCached = false;
		}

		void Polygon::cut(uint32_t fromVertexIndex, uint32_t toVertexIndex, IndexVector const& vertexIndices, IndexVector* newEdgeIndices, DirectedEdgeVector* removedEdges)
		{
			DirectedEdgeLoop::cut(fromVertexIndex, toVertexIndex, vertexIndices, newEdgeIndices, removedEdges);

			// Remove holes
			IndexList keepIndices;
			for (uint32_t holeIndex: mHoleIndices)
			{
				auto const& hole = mwMesh->getPolygon(holeIndex);

				// Check if first vertex is inside this polygon
				auto const& holeVertex0 = mwMesh->getVertex(hole.getVertexIndexList().front());
				if (pointInsideConvexHull(holeVertex0.getPosition()))
				{
					// Keep this hole
					keepIndices.push_back(holeIndex);
				}
			}

			mHoleIndices = keepIndices;
		}

		void Polygon::addHole(Polygon& hole)
		{
			// Check this isn't a hole
			if (isHole())
			{
				string errMsg = std::format("cannot add a hole to polygon {} because it is a hole itself.", mMeshIndex);
				throw GeometryOperationException(__FUNCTION__, errMsg, true);
			}

			hole.convertToHole();
			mHoleIndices.push_back(hole.getMeshIndex());
		}

		void Polygon::removeHole(uint32_t holeIndex)
		{
			mHoleIndices.remove(holeIndex);
			mwMesh->removePolygon(holeIndex);
		}

		void Polygon::convertToHole()
		{
			mHoleIndices.clear();
			setWinding(Winding::Clockwise);
		}

		void Polygon::convertFromHole()
		{
			setWinding(Winding::Anticlockwise);
		}

		bool Polygon::isHole() const
		{
			return mWinding == Winding::Clockwise;
		}

		IndexList const& Polygon::getHoleIndices() const
		{
			return mHoleIndices;
		}

		bool Polygon::pointInside(Vector2 const& point) const
		{
			return pointInside(point.x, point.y);
		}

		bool Polygon::pointInside(float x, float y) const
		{
			return getTriangulation().pointInside(x, y, mwMesh);
		}

		void Polygon::invalidateEdgeData()
		{
			invalidateTriangleData();
		}

		size_t Polygon::getTriangulationTriangleCount() const
		{
			return getTriangulation().getNumTriangles();
		}

		void Polygon::getTriangulationVertexIndices(size_t triangleIndex, uint32_t& v0, uint32_t& v1, uint32_t& v2) const
		{
			return getTriangulation().getVertexIndices(triangleIndex, v0, v1, v2);
		}


		wp::Triangulation Polygon::createBasicTriangulation() const
		{
			return getTriangulation().createBasicTriangulation(mwMesh);
		}


	} // geometry
} //WP_NAMESPACE
