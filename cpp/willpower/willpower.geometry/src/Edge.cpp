#include <cassert>

#include "willpower/geometry/Edge.h"
#include "willpower/geometry/Mesh.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace WP_NAMESPACE;

		Edge::Edge()
			: Edge(-1, -1)
		{
		}

		Edge::Edge(int vertex0, int vertex1)
			: mwMesh(nullptr)
			, mMeshIndex(-1)
			, mDeleteFunction({})
			, mUpdateVerticesFunction({})
			, mLength(0.0f)
			, mPublicId(-1)
			, mAttributeIndex(-1)
		{
			mVertices[0] = vertex0;
			mVertices[1] = vertex1;

			updateInternals();
		}

		Edge::Edge(Edge const& other)
		{
			copyFrom(other);
		}

		Edge& Edge::operator=(Edge const& other)
		{
			copyFrom(other);
			return *this;
		}

		bool Edge::operator==(Edge const& other) const
		{
			return mPublicId == other.mPublicId;
		}

		bool Edge::operator!=(Edge const& other) const
		{
			return !(*this == other);
		}

		void Edge::copyFrom(Edge const& other)
		{
			mwMesh = other.mwMesh;
			mMeshIndex = other.mMeshIndex;
			mPublicId = other.mPublicId;
			mAttributeIndex = other.mAttributeIndex;

			mVertices[0] = other.mVertices[0];
			mVertices[1] = other.mVertices[1];

			mPolygonRefs = other.mPolygonRefs;

			mDeleteFunction = other.mDeleteFunction;
			mUpdateVerticesFunction = other.mUpdateVerticesFunction;

			// Cached internals
			mNormal = other.mNormal;
			mCentre = other.mCentre;
			mDirection = other.mDirection;
			mLength = other.mLength;
		}

		int32_t Edge::getPublicId() const
		{
			return mPublicId;
		}

		void Edge::updateInternals()
		{
			if (mVertices[0] >= 0 && mVertices[1] >= 0 && mwMesh != nullptr)
			{
				auto const& v0 = mwMesh->getVertex(mVertices[0]).getPosition();
				auto const& v1 = mwMesh->getVertex(mVertices[1]).getPosition();

				mNormal = (v1 - v0).perpendicular().normalisedCopy();
				mCentre = v0.lerp(v1, 0.5f);
				mLength = v0.distanceTo(v1);
				mDirection = v0.directionTo(v1);
			}
		}

		void Edge::setMesh(Mesh* mesh, int32_t index)
		{
			mwMesh = mesh;
			mMeshIndex = index;
			updateInternals();
		}

		void Edge::setDeleteFunction(DeleteFunction function)
		{
			mDeleteFunction = function;
		}

		void Edge::setUpdateRefFunction(UpdateRefFunction function)
		{
			mUpdateVerticesFunction = function;
		}

		void Edge::addPolygonReference(uint32_t id)
		{
			mPolygonRefs.insert(id);
		}

		void Edge::_removePolygonReference(uint32_t id, bool deleteIfOrphaned)
		{
			mPolygonRefs.erase(id);

			if (mPolygonRefs.empty() && deleteIfOrphaned)
			{
				mDeleteFunction(mwMesh, (uint32_t)mMeshIndex);
			}
		}

		void Edge::updatePolygonReference(uint32_t oldId, uint32_t newId)
		{
			mPolygonRefs.erase(oldId);
			mPolygonRefs.insert(newId);
		}

		IndexSet const& Edge::getPolygonReferences() const
		{
			return mPolygonRefs;
		}

		void Edge::setVertices(int32_t vertex0, int32_t vertex1)
		{
			if (mUpdateVerticesFunction)
			{
				mUpdateVerticesFunction(mwMesh, (uint32_t)mMeshIndex, mVertices[0], vertex0, mVertices[1], vertex1);
			}

			mVertices[0] = vertex0;
			mVertices[1] = vertex1;
		
			updateInternals();
		}

		void Edge::setFirstVertex(int32_t vertex)
		{
			if (mUpdateVerticesFunction)
			{
				mUpdateVerticesFunction(mwMesh, (uint32_t)mMeshIndex, mVertices[0], vertex, mVertices[1], mVertices[1]);
			}

			mVertices[0] = vertex;
			updateInternals();
		}

		void Edge::setSecondVertex(int32_t vertex)
		{
			if (mUpdateVerticesFunction)
			{
				mUpdateVerticesFunction(mwMesh, (uint32_t)mMeshIndex, mVertices[0], mVertices[0], mVertices[1], vertex);
			}

			mVertices[1] = vertex;
			updateInternals();
		}

		int32_t Edge::getFirstVertex() const
		{
			return mVertices[0];
		}

		int32_t Edge::getSecondVertex() const
		{
			return mVertices[1];
		}

		int32_t Edge::getOtherVertex(int32_t vertex) const
		{
			if (mVertices[0] == vertex)
			{
				return mVertices[1];
			}
			if (mVertices[1] == vertex)
			{
				return mVertices[0];
			}

			return -1;
		}

		Vector2 const& Edge::getCentre() const
		{
			return mCentre;
		}

		Vector2 const& Edge::getNormal() const
		{
			return mNormal;
		}

		Vector2 const& Edge::getDirection() const
		{
			return mDirection;
		}

		float Edge::getLength() const
		{
			return mLength;
		}

		Vector2 Edge::getClosestPoint(Vector2 const& point) const
		{
			auto const& vertex0 = mwMesh->getVertex(mVertices[0]);
			auto const& vertex1 = mwMesh->getVertex(mVertices[1]);

			return point.closestPointOnLine(vertex0.getPosition(), vertex1.getPosition());
		}

		float Edge::getDistanceTo(Vector2 const& point) const
		{
			return point.distanceToLine(mwMesh->getVertex(mVertices[0]).getPosition(), mwMesh->getVertex(mVertices[1]).getPosition());
		}

		Vector2 Edge::lerp(float t) const
		{
			auto const& v0 = mwMesh->getVertex(mVertices[0]);
			auto const& v1 = mwMesh->getVertex(mVertices[1]);

			return v0.getPosition().lerp(v1.getPosition(), t);
		}

		bool Edge::connectedTo(Edge const& edge) const
		{
			return (mVertices[0] == edge.getFirstVertex() ||
				mVertices[0] == edge.getSecondVertex() ||
				mVertices[1] == edge.getFirstVertex() ||
				mVertices[1] == edge.getSecondVertex());
		}

		int32_t Edge::getSharedVertexIndex(Edge const& edge) const
		{
			int32_t v = edge.getOtherVertex(mVertices[0]);
			if (v >= 0)
			{
				return mVertices[0];
			}

			v = edge.getOtherVertex(mVertices[1]);
			if (v >= 0)
			{
				return mVertices[1];
			}

			return -1;
		}

		BoundingBox Edge::getBoundingBox() const
		{
			Vector2 vertex0pos = mwMesh->getVertex(mVertices[0]).getPosition();
			Vector2 vertex1pos = mwMesh->getVertex(mVertices[1]).getPosition();

			float x0 = (std::min)(vertex0pos.x, vertex1pos.x);
			float y0 = (std::min)(vertex0pos.y, vertex1pos.y);
			float x1 = (std::max)(vertex0pos.x, vertex1pos.x);
			float y1 = (std::max)(vertex0pos.y, vertex1pos.y);

			return BoundingBox(x0, y0, x1 - x0, y1 - y0);
		}

		Edge::Connectivity Edge::getConnectivity() const
		{
			switch (mPolygonRefs.size())
			{
			case 0:
				return Connectivity::Orphaned;

			case 1:
				return Connectivity::External;

			case 2:
				// If one is a hole, then internal, else external
				return mwMesh->getPolygon(*mPolygonRefs.begin()).isHole() || mwMesh->getPolygon(*++mPolygonRefs.begin()).isHole() ?
					Connectivity::External : Connectivity::Internal;

			case 3: 
				// 3 polygons if its a member of a hole
				return Connectivity::Internal;

			default:
				assert(false && "Edge::getConnectivity() edge is a member of more than 2 polygons.");
				return Connectivity::Invalid;
			}
		}

	} // geometry
} //WP_NAMESPACE
