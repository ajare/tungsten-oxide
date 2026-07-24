#pragma once

#include <vector>

#include "willpower/common/BatchRenderable.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API IndexedTriangleBatchRenderable : public BatchRenderable
	{
		std::vector<int8_t> mIndexData;

		int mBatchCount, mVertexCount, mIndexWidth;

	public:

		IndexedTriangleBatchRenderable(int triangleCount, int indexWidth, int vertexCount, int posStride, int texStride, int colourOffset);

		int8_t* getIndexData();

		void resizeIndexData(int requiredSize);

		void setBatchCount(int count);

		int getBatchCount() const;

		void setVertexCount(int count);

		int getVertexCount() const;

		int getIndexWidth() const;

	};

} // WP_NAMESPACE
