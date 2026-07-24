#include "willpower/geometry/VertexFilter.h"
#include "willpower/geometry/MeshQuery.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace WP_NAMESPACE;
		using namespace std;

		VertexFilter::VertexFilter(Mesh const* mesh)
			: Filter(mesh)
		{
		}

		VertexFilter& VertexFilter::selectVertices(IndexSet const& vertexIndices)
		{
			setIndices(vertexIndices);
			return *this;
		}

		VertexFilter& VertexFilter::selectVertices(BoundingCircle const& bounds)
		{
			MeshQuery query(mwMesh);

			setIndices(query.getVertexIndicesInBoundingObject(bounds));
			return *this;
		}

		VertexFilter& VertexFilter::selectVertices(BoundingBox const& bounds)
		{
			MeshQuery query(mwMesh);

			setIndices(query.getVertexIndicesInBoundingObject(bounds));
			return *this;
		}

		VertexFilter& VertexFilter::selectEdgeVertices(uint32_t edgeIndex)
		{
			auto const& edge = mwMesh->getEdge(edgeIndex);
			setIndices({ (uint32_t)edge.getFirstVertex(), (uint32_t)edge.getSecondVertex() });
			return *this;
		}

		VertexFilter& VertexFilter::selectPolygonVertices(uint32_t polygonIndex)
		{
			auto const& polygon = mwMesh->getPolygon(polygonIndex);
			setIndices(polygon.getVertexIndexSet());
			return *this;
		}

		VertexFilter& VertexFilter::addVertices(IndexSet const& vertexIndices)
		{
			addIndices(vertexIndices);
			return *this;
		}

		VertexFilter& VertexFilter::addVertices(BoundingCircle const& bounds)
		{
			MeshQuery query(mwMesh);

			addIndices(query.getVertexIndicesInBoundingObject(bounds));
			return *this;
		}

		VertexFilter& VertexFilter::addVertices(BoundingBox const& bounds)
		{
			MeshQuery query(mwMesh);

			addIndices(query.getVertexIndicesInBoundingObject(bounds));
			return *this;
		}

		VertexFilter& VertexFilter::addEdgeVertices(uint32_t edgeIndex)
		{
			auto const& edge = mwMesh->getEdge(edgeIndex);
			addIndices({ (uint32_t)edge.getFirstVertex(), (uint32_t)edge.getSecondVertex() });
			return *this;
		}

		VertexFilter& VertexFilter::addPolygonVertices(uint32_t polygonIndex)
		{
			auto const& polygon = mwMesh->getPolygon(polygonIndex);
			addIndices(polygon.getVertexIndexSet());
			return *this;
		}

		VertexFilter& VertexFilter::minimumX()
		{
			auto func = [this](uint32_t vertexIndex0, uint32_t vertexIndex1)
			{
				auto const& vertex0 = this->mwMesh->getVertex(vertexIndex0);
				auto const& vertex1 = this->mwMesh->getVertex(vertexIndex1);

				return vertex0.getPosition().x < vertex1.getPosition().x;
			};

			minElement(func);
			return *this;
		}

		VertexFilter& VertexFilter::maximumX()
		{
			auto func = [this](uint32_t vertexIndex0, uint32_t vertexIndex1)
			{
				auto const& vertex0 = this->mwMesh->getVertex(vertexIndex0);
				auto const& vertex1 = this->mwMesh->getVertex(vertexIndex1);

				return vertex0.getPosition().x < vertex1.getPosition().x;
			};

			maxElement(func);
			return *this;
		}

		VertexFilter& VertexFilter::minimumY()
		{
			auto func = [this](uint32_t vertexIndex0, uint32_t vertexIndex1)
			{
				auto const& vertex0 = this->mwMesh->getVertex(vertexIndex0);
				auto const& vertex1 = this->mwMesh->getVertex(vertexIndex1);

				return vertex0.getPosition().y < vertex1.getPosition().y;
			};

			minElement(func);
			return *this;
		}

		VertexFilter& VertexFilter::maximumY()
		{
			auto func = [this](uint32_t vertexIndex0, uint32_t vertexIndex1)
			{
				auto const& vertex0 = this->mwMesh->getVertex(vertexIndex0);
				auto const& vertex1 = this->mwMesh->getVertex(vertexIndex1);

				return vertex0.getPosition().y < vertex1.getPosition().y;
			};

			maxElement(func);
			return *this;
		}
	} // geometry
} //WP_NAMESPACE
