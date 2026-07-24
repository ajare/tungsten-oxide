#pragma once

#include <vector>

#include "willpower/common/BatchRenderable.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API TriangleBatchRenderable : public BatchRenderable
	{
		int mBatchCount, mVertexCount;

	public:

		TriangleBatchRenderable(int vertexCount, int posStride, int texStride, int colourOffset);

		void setBatchCount(int count);

		int getBatchCount() const;

		void setVertexCount(int count);

		int getVertexCount() const;

	};

} // WP_NAMESPACE
