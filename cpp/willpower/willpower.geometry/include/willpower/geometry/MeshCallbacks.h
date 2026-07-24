#pragma once

#include <vector>
#include <functional>


#include "willpower/geometry/Platform.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{
		typedef std::function<void(uint32_t, int32_t)> AddMeshItemCallback;

		typedef std::function<void(uint32_t)> RemoveMeshItemCallback;

		typedef std::function<void(uint32_t, uint32_t)> UpdateMeshItemCallback;

		struct MeshCallbacks
		{
			AddMeshItemCallback onAddVertex, onAddEdge, onAddPolygon;
			RemoveMeshItemCallback onRemoveVertex, onRemoveEdge, onRemovePolygon;
			UpdateMeshItemCallback onUpdateVertex, onUpdateEdge, onUpdatePolygon;

		public:

			MeshCallbacks()
				: onAddVertex({})
				, onAddEdge({})
				, onAddPolygon({})
				, onRemoveVertex({})
				, onRemoveEdge({})
				, onRemovePolygon({})
				, onUpdateVertex({})
				, onUpdateEdge({})
				, onUpdatePolygon({})
			{
			}
		};


	} // geometry
} // WP_NAMESPACE
