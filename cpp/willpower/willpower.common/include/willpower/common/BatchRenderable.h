#pragma once

#include <vector>

#include "willpower/common/Renderable.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API BatchRenderable : public Renderable
	{
	protected:

		int mNumPrimitives;

		int mPosStride, mTexStride;

		int mColourOffset;

		std::vector<int8_t> mPositionData;

		std::vector<int8_t> mTexcoordData;

	public:

		BatchRenderable(int count, int posStride, int texStride, int colourOffset);

		void setNumPrimitives(int numPrimitives);

		int getNumPrimitives() const;

		int getPositionStride() const;

		int getTexcoordStride() const;

		int8_t* getPositionData(int index = 0);

		int getPositionDataSize() const;

		virtual int8_t* getColourData(int index = 0);

		int8_t* getTexcoordData(int index = 0);

		int getTexcoordDataSize() const;

		void resizePositionData(int requiredSize);

		void resizeTexcoordData(int requiredSize);

		int getVertexDataSize() const;
	};

} // WP_NAMESPACE
