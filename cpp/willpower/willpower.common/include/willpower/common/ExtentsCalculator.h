#pragma once

#include <cstdint>

#include "willpower/common/Platform.h"
#include "willpower/common/Vector2.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API ExtentsCalculator
	{
		wp::Vector2 mMinExtent, mMaxExtent;

		float mPadding;

	public:

		ExtentsCalculator(wp::Vector2 const& minExtent, wp::Vector2 const& maxExtent, float padding);
	
		Vector2 getMinExtent() const;

		Vector2 getMaxExtent() const;

		Vector2 getSize() const;

		Vector2 getCentre() const;

		Vector2 getCellSize(uint32_t numCellsX, uint32_t numCellsY) const;
	};

} // WP_NAMESPACE
