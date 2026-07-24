#include "willpower/geometry/PolygonFilter.h"
#include "willpower/geometry/MeshQuery.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace WP_NAMESPACE;
		using namespace std;

		PolygonFilter::PolygonFilter(Mesh const* mesh)
			: Filter(mesh)
		{
		}

		PolygonFilter& PolygonFilter::selectPolygons(IndexSet const& polygonIndices)
		{
			setIndices(polygonIndices);
			return *this;
		}

		PolygonFilter& PolygonFilter::selectPolygons(BoundingCircle const& bounds)
		{
			MeshQuery query(mwMesh);

			setIndices(query.getPolygonIndicesInBoundingObject(bounds));
			return *this;
		}

		PolygonFilter& PolygonFilter::selectPolygons(BoundingBox const& bounds)
		{
			MeshQuery query(mwMesh);

			setIndices(query.getPolygonIndicesInBoundingObject(bounds));
			return *this;
		}

		PolygonFilter& PolygonFilter::addPolygons(IndexSet const& polygonIndices)
		{
			addIndices(polygonIndices);
			return *this;
		}

		PolygonFilter& PolygonFilter::addPolygons(BoundingCircle const& bounds)
		{
			MeshQuery query(mwMesh);

			addIndices(query.getPolygonIndicesInBoundingObject(bounds));
			return *this;
		}

		PolygonFilter& PolygonFilter::addPolygons(BoundingBox const& bounds)
		{
			MeshQuery query(mwMesh);

			addIndices(query.getPolygonIndicesInBoundingObject(bounds));
			return *this;
		}

	} // geometry
} //WP_NAMESPACE
