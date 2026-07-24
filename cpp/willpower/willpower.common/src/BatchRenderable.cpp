#include <algorithm>

#include "willpower/common/BatchRenderable.h"

using namespace std;

namespace WP_NAMESPACE
{

	BatchRenderable::BatchRenderable(int count, int posStride, int texStride, int colourOffset)
		: mNumPrimitives(0)
		, mPosStride(posStride)
		, mTexStride(texStride)
		, mColourOffset(colourOffset)
	{
		resizePositionData(count * posStride);
		resizeTexcoordData(count * texStride);
	}

	void BatchRenderable::setNumPrimitives(int numPrimitives)
	{
		mNumPrimitives = numPrimitives;
	}

	int BatchRenderable::getNumPrimitives() const
	{
		return mNumPrimitives;
	}

	int BatchRenderable::getPositionStride() const
	{
		return mPosStride;
	}

	int BatchRenderable::getTexcoordStride() const
	{
		return mTexStride;
	}

	int8_t* BatchRenderable::getPositionData(int index)
	{
		return &(mPositionData[mPosStride * index]);
	}

	int BatchRenderable::getPositionDataSize() const
	{
		return (int)mPositionData.size();
	}

	int8_t* BatchRenderable::getColourData(int index)
	{
		return &(mPositionData[mPosStride * index + mColourOffset]);
	}

	int8_t* BatchRenderable::getTexcoordData(int index)
	{
		return &(mTexcoordData[mTexStride * index]);
	}

	int BatchRenderable::getTexcoordDataSize() const
	{
		return (int)mTexcoordData.size();
	}

	void BatchRenderable::resizePositionData(int requiredSize)
	{
		if (requiredSize > (int)mPositionData.size())
		{
			mPositionData.resize(requiredSize);
		}
	}

	void BatchRenderable::resizeTexcoordData(int requiredSize)
	{
		if (requiredSize > (int)mTexcoordData.size())
		{
			mTexcoordData.resize(requiredSize);
		}
	}

	int BatchRenderable::getVertexDataSize() const
	{
		return (int)(mPositionData.size() + mTexcoordData.size());
	}

} // WP_NAMESPACE
