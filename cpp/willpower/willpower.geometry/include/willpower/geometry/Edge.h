#pragma once

#include <set>
#include <functional>
#include <algorithm>

#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingBox.h"

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Types.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class Mesh;

		class WP_GEOMETRY_API Edge
		{
			friend class Mesh;

		public:

			enum Connectivity
			{
				Orphaned,
				External,
				Internal,
				Invalid,
				COUNT
			};

		public:

			typedef std::function<void(Mesh*, uint32_t)> DeleteFunction;

			typedef std::function<void(Mesh*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)> UpdateRefFunction;

		private:

			Mesh* mwMesh;

			int32_t mMeshIndex;

			int32_t mPublicId;

			int32_t mAttributeIndex;

			int32_t mVertices[2];

			IndexSet mPolygonRefs;

			DeleteFunction mDeleteFunction;

			UpdateRefFunction mUpdateVerticesFunction;

			// Cached internals
			Vector2 mNormal, mCentre, mDirection;

			float mLength;

		private:

			void copyFrom(Edge const& other);

			void setMesh(Mesh* mesh, int32_t index);

			void setDeleteFunction(DeleteFunction func);

			void setUpdateRefFunction(UpdateRefFunction func);

			void addPolygonReference(uint32_t id);

			void _removePolygonReference(uint32_t id, bool deleteIfOrphaned);

			void updatePolygonReference(uint32_t oldId, uint32_t newId);

			void updateInternals();

		public:

			Edge();

			Edge(int vertex0, int vertex1);

			Edge(Edge const& other);

			Edge& operator=(Edge const& other);

			bool operator==(Edge const& other) const;

			bool operator!=(Edge const& other) const;

			int32_t getPublicId() const;

			void setVertices(int32_t vertex0, int32_t vertex1);

			void setFirstVertex(int32_t vertex);

			void setSecondVertex(int32_t vertex);

			int32_t getFirstVertex() const;

			int32_t getSecondVertex() const;

			int32_t getOtherVertex(int32_t vertex) const;

			Vector2 const& getCentre() const;

			Vector2 const& getNormal() const;

			Vector2 const& getDirection() const;
			
			float getLength() const;

			BoundingBox getBoundingBox() const;

			IndexSet const& getPolygonReferences() const;

			Connectivity getConnectivity() const;

			// Utility
			Vector2 getClosestPoint(Vector2 const& point) const;

			float getDistanceTo(Vector2 const& point) const;

			Vector2 lerp(float t) const;

			bool connectedTo(Edge const& edge) const;

			int32_t getSharedVertexIndex(Edge const& edge) const;
		};

		struct UndirectedEdgeComparer
		{
			bool operator()(Edge const& a, Edge const& b) const
			{
				int32_t aMin = (std::min)(a.getFirstVertex(), a.getSecondVertex());
				int32_t aMax = (std::max)(a.getFirstVertex(), a.getSecondVertex());
				int32_t bMin = (std::min)(b.getFirstVertex(), b.getSecondVertex());
				int32_t bMax = (std::max)(b.getFirstVertex(), b.getSecondVertex());

				if (aMin < bMin)
				{
					return true;
				}
				else if (aMin > bMin)
				{
					return false;
				}
				else
				{
					return aMax < bMax;
				}
			}
		};

	} // geometry
} // WP_NAMESPACE
