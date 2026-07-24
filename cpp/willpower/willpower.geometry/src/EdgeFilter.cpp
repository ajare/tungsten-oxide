#include "willpower/geometry/EdgeFilter.h"
#include "willpower/geometry/MeshQuery.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace WP_NAMESPACE;
		using namespace std;

		EdgeFilter::EdgeFilter(Mesh const* mesh)
			: Filter(mesh)
		{
		}

		EdgeFilter& EdgeFilter::selectEdges(IndexSet const& edgeIndices)
		{
			setIndices(edgeIndices);
			return *this;
		}

		EdgeFilter& EdgeFilter::selectEdges(BoundingCircle const& bounds)
		{
			MeshQuery query(mwMesh);

			setIndices(query.getEdgeIndicesInBoundingObject(bounds));
			return *this;
		}

		EdgeFilter& EdgeFilter::selectEdges(BoundingBox const& bounds)
		{
			MeshQuery query(mwMesh);

			setIndices(query.getEdgeIndicesInBoundingObject(bounds));
			return *this;
		}

		EdgeFilter& EdgeFilter::selectPolygonEdges(uint32_t polygonIndex)
		{
			auto const& polygon = mwMesh->getPolygon(polygonIndex);
			setIndices(polygon.getEdgeIndexSet());
			return *this;
		}

		EdgeFilter& EdgeFilter::addEdges(IndexSet const& edgeIndices)
		{
			addIndices(edgeIndices);
			return *this;
		}

		EdgeFilter& EdgeFilter::addEdges(BoundingCircle const& bounds)
		{
			MeshQuery query(mwMesh);

			addIndices(query.getEdgeIndicesInBoundingObject(bounds));
			return *this;
		}

		EdgeFilter& EdgeFilter::addEdges(BoundingBox const& bounds)
		{
			MeshQuery query(mwMesh);

			addIndices(query.getEdgeIndicesInBoundingObject(bounds));
			return *this;
		}

		EdgeFilter& EdgeFilter::addPolygonEdges(uint32_t polygonIndex)
		{
			auto const& polygon = mwMesh->getPolygon(polygonIndex);
			addIndices(polygon.getEdgeIndexSet());
			return *this;
		}

		EdgeFilter& EdgeFilter::normalAngle(float angle, float tolerance)
		{
			auto func = [this, angle, tolerance](uint32_t edgeIndex)
			{
				auto const& edge = this->mwMesh->getEdge(edgeIndex);

				Vector2 angleDir = Vector2::fromAngle(angle, Winding::Anticlockwise);
				float dAngle = angleDir.minimumAngleTo(edge.getNormal());

				return dAngle <= tolerance;
			};

			filter(func);
			return *this;
		}

		EdgeFilter& EdgeFilter::minimumCentreX()
		{
			auto func = [this](uint32_t edgeIndex0, uint32_t edgeIndex1)
			{
				auto const& edge0 = this->mwMesh->getEdge(edgeIndex0);
				auto const& edge1 = this->mwMesh->getEdge(edgeIndex1);

				return edge0.getCentre().x < edge1.getCentre().x;
			};

			minElement(func);
			return *this;
		}

		EdgeFilter& EdgeFilter::maximumCentreX()
		{
			auto func = [this](uint32_t edgeIndex0, uint32_t edgeIndex1)
			{
				auto const& edge0 = this->mwMesh->getEdge(edgeIndex0);
				auto const& edge1 = this->mwMesh->getEdge(edgeIndex1);

				return edge0.getCentre().x < edge1.getCentre().x;
			};

			maxElement(func);
			return *this;
		}

		EdgeFilter& EdgeFilter::minimumCentreY()
		{
			auto func = [this](uint32_t edgeIndex0, uint32_t edgeIndex1)
			{
				auto const& edge0 = this->mwMesh->getEdge(edgeIndex0);
				auto const& edge1 = this->mwMesh->getEdge(edgeIndex1);

				return edge0.getCentre().y < edge1.getCentre().y;
			};

			minElement(func);
			return *this;
		}

		EdgeFilter& EdgeFilter::maximumCentreY()
		{
			auto func = [this](uint32_t edgeIndex0, uint32_t edgeIndex1)
			{
				auto const& edge0 = this->mwMesh->getEdge(edgeIndex0);
				auto const& edge1 = this->mwMesh->getEdge(edgeIndex1);

				return edge0.getCentre().y < edge1.getCentre().y;
			};

			maxElement(func);
			return *this;
		}

	} // geometry
} //WP_NAMESPACE
