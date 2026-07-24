#pragma once

#include <set>
#include <functional>

#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingBox.h"

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Types.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class Mesh;

		class WP_GEOMETRY_API Vertex
		{
			friend class Mesh;
			friend class MeshOperations;

		public:

			typedef std::function<void(Mesh*, uint32_t)> DeleteFunction;

			typedef std::function<void(Mesh*, uint32_t)> UpdateEdgeFunction;

		private:

			Mesh* mwMesh;

			int32_t mMeshIndex;

			int32_t mPublicId;

			int32_t mAttributeIndex;

			Vector2 mPosition;

			IndexSet mEdgeRefs;

			DeleteFunction mDeleteFunction;

			UpdateEdgeFunction mUpdateEdgeFunction;

		private:

			void copyFrom(Vertex const& other);

			void setMesh(Mesh* mesh, int32_t index);

			void setDeleteFunction(DeleteFunction func);

			void setUpdateEdgeFunction(UpdateEdgeFunction func);

			void addEdgeReference(uint32_t id);

			void removeEdgeReference(uint32_t id);

			void updateEdgeReference(uint32_t oldId, uint32_t newId);

			void updateReferencedEdges();

			void setPosition(Vector2 const& position);

			void translatePosition(Vector2 const& position);

		public:

			explicit Vertex(Vector2 const& position);

			Vertex(float x, float y);
			
			Vertex(Vertex const& other);

			~Vertex();

			Vertex& operator=(Vertex const& other);

			bool operator==(Vertex const& other) const;

			bool operator!=(Vertex const& other) const;

			int32_t getPublicId() const;

			Vector2 const& getPosition() const;

			BoundingBox getBoundingBox() const;

			IndexSet const& getEdgeReferences() const;
		};

	} // geometry
} // WP_NAMESPACE
