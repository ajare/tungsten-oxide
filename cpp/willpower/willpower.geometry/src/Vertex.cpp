#include "willpower/geometry/Vertex.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace WP_NAMESPACE;

		Vertex::Vertex(Vector2 const& position)
			: mwMesh(nullptr)
			, mMeshIndex(-1)
			, mPublicId(-1)
			, mAttributeIndex(-1)
			, mPosition(position)
			, mDeleteFunction({})
			, mUpdateEdgeFunction({})
		{
		}

		Vertex::Vertex(float x, float y)
			: Vertex(Vector2(x, y))
		{
		}

		Vertex::Vertex(Vertex const& other)
		{
			copyFrom(other);
		}

		Vertex::~Vertex()
		{
		}

		Vertex& Vertex::operator=(Vertex const& other)
		{
			copyFrom(other);
			return *this;
		}

		bool Vertex::operator==(Vertex const& other) const
		{
			return mPublicId == other.mPublicId;
		}

		bool Vertex::operator!=(Vertex const& other) const
		{
			return !(*this == other);
		}

		int32_t Vertex::getPublicId() const
		{
			return mPublicId;
		}

		void Vertex::copyFrom(Vertex const& other)
		{
			mwMesh = other.mwMesh;
			mMeshIndex = other.mMeshIndex;
			mPublicId = other.mPublicId;
			mAttributeIndex = other.mAttributeIndex;
			mPosition = other.mPosition;
			mEdgeRefs = other.mEdgeRefs;
			mDeleteFunction = other.mDeleteFunction;
			mUpdateEdgeFunction = other.mUpdateEdgeFunction;
		}

		void Vertex::setMesh(Mesh* mesh, int32_t index)
		{
			mwMesh = mesh;
			mMeshIndex = index;
		}

		void Vertex::setDeleteFunction(DeleteFunction func)
		{
			mDeleteFunction = func;
		}

		void Vertex::setUpdateEdgeFunction(UpdateEdgeFunction func)
		{
			mUpdateEdgeFunction = func;
		}

		void Vertex::addEdgeReference(uint32_t id)
		{
			mEdgeRefs.insert(id);
		}

		void Vertex::removeEdgeReference(uint32_t id)
		{
			mEdgeRefs.erase(id);

			if (mEdgeRefs.empty())
			{
				mDeleteFunction(mwMesh, mMeshIndex);
			}
		}

		void Vertex::updateEdgeReference(uint32_t oldId, uint32_t newId)
		{
			mEdgeRefs.erase(oldId);
			mEdgeRefs.insert(newId);
		}

		void Vertex::updateReferencedEdges()
		{
			if (mUpdateEdgeFunction)
			{
				for (uint32_t edgeRef: mEdgeRefs)
				{
					mUpdateEdgeFunction(mwMesh, edgeRef);
				}
			}
		}

		IndexSet const& Vertex::getEdgeReferences() const
		{
			return mEdgeRefs;
		}

		void Vertex::setPosition(Vector2 const& position)
		{
			mPosition = position;
			updateReferencedEdges();
		}

		void Vertex::translatePosition(Vector2 const& position)
		{
			mPosition += position;
			updateReferencedEdges();
		}

		Vector2 const& Vertex::getPosition() const
		{
			return mPosition;
		}

		BoundingBox Vertex::getBoundingBox() const
		{
			return BoundingBox(mPosition, Vector2::ZERO);
		}

	} // geometry
} // WP_NAMESPACE