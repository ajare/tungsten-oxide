#pragma once

#include <vector>

#include "willpower/common/IndexedTriangleBatchRenderable.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API TriangleStripBatchRenderable : public IndexedTriangleBatchRenderable
	{
	public:

		TriangleStripBatchRenderable(int triangleCount, int indexWidth, int posStride, int texStride, int colourOffset);

	};

} // WP_NAMESPACE
