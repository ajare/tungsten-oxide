#pragma once

#include <vector>

#include <mpp/helper/LineBatchDataProvider.h>

#include <willpower/common/BoundingBox.h>

#include <core/Stats.h>

#include "Platform.h"


class MetricsDataProvider : public mpp::helper::LineBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>
{
	struct Line
	{
		float time;
		wp::Vector2 v[2];
		uint8_t c[4];
	};

private:

	wp::BoundingBox mBounds;

	std::vector<Line> mLines;

	// Use a ring buffer
	uint32_t mCapacity, mReadIndex, mWriteIndex;

	float mHistory;

	float mTimer;

private:

	Line const& getReadLine(int index) const
	{
		auto i = ((int)mReadIndex + index) % mCapacity;
		return mLines[i];
	}

	Line& getWriteLine(int index)
	{
		auto i = ((int)mWriteIndex + index) % mCapacity;
		return mLines[i];
	}

public:

	MetricsDataProvider(float history)
		: mCapacity((uint32_t)(1000 * history))
		, mReadIndex(mCapacity)
		, mWriteIndex(0)
		, mHistory(history)
		, mTimer(0.0f)
	{
		// Assume we won't be going above 1000 FPS
		mLines.resize(mCapacity);
	}

	void getBounds(glm::vec3& bMin, glm::vec3& bMax) override
	{
		wp::Vector2 minExtent, maxExtent;

		mBounds.getExtents(minExtent, maxExtent);

		bMin = { minExtent.x, 0.0f, minExtent.y };
		bMax = { maxExtent.x, 0.0f, maxExtent.y };
	}

	void position(uint32_t index, float& x0, float& y0, float& x1, float& y1)
	{
		auto const& line = getReadLine(index);

		x0 = line.v[0].x;
		y0 = line.v[0].y;
		x1 = line.v[1].x;
		y1 = line.v[1].y;
	}

	void colour(uint32_t index, uint8_t& red, uint8_t& green, uint8_t& blue, uint8_t& alpha)
	{
		auto const& line = getReadLine(index);

		red = line.c[0];
		green = line.c[1];
		blue = line.c[2];
		alpha = line.c[3];
	}

	mpp::Colour diffuse()
	{
		return mpp::Colour::White;
	}

	bool update(wp::BoundingBox const& viewBounds, bw::core::Stats const& stats, float frameTime)
	{
		mBounds = viewBounds;
		mTimer += frameTime;


		Line& line = getWriteLine(0);
		
		line.time = mTimer;

		// TODO: get previously-written line's time and add frameTime to get new one,
		//       and use this for the x coordinate.  Need to check if this is the first
		//       line written!

		// Update indices

		mWriteIndex = (mWriteIndex + 1) % mCapacity;

		if (mWriteIndex == (mReadIndex % mCapacity))
		{
			mReadIndex = mWriteIndex + 1;
		}

		setNumPrimitives(mWriteIndex > (mReadIndex % mCapacity) ? mWriteIndex : mCapacity);
		return true;
	}
};
