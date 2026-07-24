#pragma once

#include <vector>

#include "willpower/common/TriangleBatchRenderable.h"

namespace WP_NAMESPACE
{

	TriangleBatchRenderable::TriangleBatchRenderable(int vertexCount, int posStride, int texStride, int colourOffset)
		: BatchRenderable(vertexCount, posStride, texStride, colourOffset)
		, mBatchCount(0)
		, mVertexCount(vertexCount)
	{
	}

	void TriangleBatchRenderable::setBatchCount(int count)
	{
		mBatchCount = count;
	}

	int TriangleBatchRenderable::getBatchCount() const
	{
		return mBatchCount;
	}

	void TriangleBatchRenderable::setVertexCount(int count)
	{
		mVertexCount = count;
	}

	int TriangleBatchRenderable::getVertexCount() const
	{
		return mVertexCount;
	}

} // WP_NAMESPACE
