#pragma once

#include <list>
#include <vector>
#include <array>
#include <map>

#include "willpower/common/Vector2.h"
#include "willpower/common/MathsUtils.h"
#include "willpower/common/Triangulation.h"
#include "willpower/common/BoundingBox.h"

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Edge.h"
#include "willpower/geometry/DirectedEdgeLoop.h"
#include "willpower/geometry/MeshUtils.h"
#include "willpower/geometry/Exception.h"
#include "willpower/geometry/Types.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class Mesh;

		class WP_GEOMETRY_API Polygon : public DirectedEdgeLoop
		{
			friend class Mesh;
			friend class MeshOperations;
			friend class SerializerMeshChunk;

		private:

			class Triangulation
			{
				typedef std::array<float, 2> Point;

			public:

				enum Options
				{
					NoCospatialVertices = 1,
					NoCollinearVertices = 2

				};

			private:

				std::vector<uint32_t> mIndices;

			private:

				void copyFrom(Triangulation const& other);

				std::vector<Point> preprocessLoop(IndexVector const& vertIndices, Mesh const* mesh, std::map<uint32_t, uint32_t>& mapping, uint32_t options);

			public:

				Triangulation();

				Triangulation(Triangulation const& other);

				Triangulation& operator=(Triangulation const& other);

				size_t getNumTriangles() const;

				void getVertexIndices(size_t triangleIndex, uint32_t& v0, uint32_t& v1, uint32_t& v2) const;

				bool build(Polygon const& polygon, Mesh const* mesh, uint32_t options = Options::NoCospatialVertices);

				bool pointInside(float x, float y, Mesh const* mesh) const;

				bool intersects(Triangulation const& other, Mesh const* mesh) const;

				wp::Triangulation createBasicTriangulation(Mesh const* mesh) const;
			};

		private:

			int32_t mPublicId; 

			int32_t mAttributeIndex;
			
			// Triangle data cache
			mutable bool mTriangleDataCached;
			
			mutable Triangulation mTriangleData;

			IndexList mHoleIndices;

		private:

			void copyFrom(Polygon const& other);

			void cacheTriangleData() const;

			void invalidateTriangleData();

			void invalidateEdgeData();

			void addHole(Polygon& hole);

			void removeHole(uint32_t holeIndex);

			void convertToHole();

			void convertFromHole();

		protected:

			void cut(uint32_t fromVertexIndex, uint32_t toVertexIndex, IndexVector const& vertexIndices, IndexVector* newEdgeIndices = nullptr, DirectedEdgeVector* removedEdges = nullptr);

			Triangulation const& getTriangulation() const;

		public:

			explicit Polygon(IndexVector const& edgeData);

			Polygon(Polygon const& other);

			Polygon& operator=(Polygon const& other);

			bool operator==(Polygon const& other) const;

			bool operator!=(Polygon const& other) const;

			int32_t getPublicId() const;

			bool pointInside(Vector2 const& point) const;

			bool pointInside(float x, float y) const;

			bool isHole() const;

			IndexList const& getHoleIndices() const;

			size_t getTriangulationTriangleCount() const;

			void getTriangulationVertexIndices(size_t triangleIndex, uint32_t& v0, uint32_t& v1, uint32_t& v2) const;

			wp::Triangulation createBasicTriangulation() const;
		};

	} // geometry
} // WP_NAMESPACE
