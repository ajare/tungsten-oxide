#include <algorithm>

#include "willpower/common/WillpowerWalker.h"

#include "willpower/geometry/MeshHelpers.h"

#undef max

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace std;
		using namespace WP_NAMESPACE;

		void MeshHelpers::generatePolygonFromVertices(Mesh* mesh, vector<Vector2> const& vertexPositions, RegularPolygonCreationResult* result)
		{
			vector<uint32_t> vertexIndices;

			// Create vertices
			for (auto const& vPos : vertexPositions)
			{
				auto vertex = Vertex(vPos);
				auto vIndex = mesh->addVertex(vertex);
				vertexIndices.push_back(vIndex);
			}

			// Create edges
			vector<uint32_t> edgeIndices;
			vector<uint32_t> edgeData;
			for (size_t i = 0; i < vertexIndices.size(); ++i)
			{
				size_t j = (i + 1) % vertexIndices.size();

				auto edge = Edge(vertexIndices[i], vertexIndices[j]);
				auto eIndex = mesh->addEdge(edge);
				edgeIndices.push_back(eIndex);

				edgeData.push_back(edge.getFirstVertex());
				edgeData.push_back(edge.getSecondVertex());
				edgeData.push_back(eIndex);
			}

			// Create polygon
			auto polygon = Polygon(edgeData);
			auto pIndex = mesh->addPolygon(polygon);

			if (result)
			{
				result->polygonIndex = pIndex;
				result->vertexIndices = vertexIndices;
				result->edgeIndices = edgeIndices;
			}
		}

		void MeshHelpers::createRectangle(Mesh* mesh, Vector2 const& minExtent, Vector2 const& maxExtent, RegularPolygonCreationResult* result)
		{
			vector<Vector2> vertexPositions;

			vertexPositions.push_back(Vector2(minExtent.x, minExtent.y));
			vertexPositions.push_back(Vector2(maxExtent.x, minExtent.y));
			vertexPositions.push_back(Vector2(maxExtent.x, maxExtent.y));
			vertexPositions.push_back(Vector2(minExtent.x, maxExtent.y));

			generatePolygonFromVertices(mesh, vertexPositions, result);
		}

		void MeshHelpers::createRegularPolygon(Mesh* mesh, Vector2 const& centre, float radius, uint32_t sides, float angle, RegularPolygonCreationResult* result)
		{
			// Generate points
			vector<Vector2> vertexPositions;

			for (uint32_t i = 0; i < sides; ++i)
			{
				float vAngle = angle + 360.0f * i / (float)sides;
				auto vPos = centre + Vector2::UNIT_Y.rotatedAnticlockwiseCopy(vAngle) * radius;
				vertexPositions.push_back(vPos);
			}

			generatePolygonFromVertices(mesh, vertexPositions, result);
		}

	} // geometry
} // WP_NAMESPACE
