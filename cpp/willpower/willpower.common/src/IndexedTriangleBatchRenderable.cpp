#pragma once

#include <vector>

#include "willpower/common/IndexedTriangleBatchRenderable.h"

namespace WP_NAMESPACE
{

	IndexedTriangleBatchRenderable::IndexedTriangleBatchRenderable(int triangleCount, int indexWidth, int vertexCount, int posStride, int texStride, int colourOffset)
		: BatchRenderable(vertexCount, posStride, texStride, colourOffset)
		, mBatchCount(0)
		, mVertexCount(vertexCount)
		, mIndexWidth(indexWidth)
	{
		mIndexData.resize(triangleCount * 3 * (indexWidth / 8));
	}

	int8_t* IndexedTriangleBatchRenderable::getIndexData()
	{
		return &(mIndexData[0]);
	}

	void IndexedTriangleBatchRenderable::resizeIndexData(int requiredSize)
	{
		if (requiredSize > (int)mIndexData.size())
		{
			mIndexData.resize(requiredSize);
		}
	}

	void IndexedTriangleBatchRenderable::setBatchCount(int count)
	{
		mBatchCount = count;
	}

	int IndexedTriangleBatchRenderable::getBatchCount() const
	{
		return mBatchCount;
	}

	void IndexedTriangleBatchRenderable::setVertexCount(int count)
	{
		mVertexCount = count;
	}

	int IndexedTriangleBatchRenderable::getVertexCount() const
	{
		return mVertexCount;
	}

	int IndexedTriangleBatchRenderable::getIndexWidth() const
	{
		return mIndexWidth;
	}

} // WP_NAMESPACE
