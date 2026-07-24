#pragma once

#include <vector>
#include <map>
#include <set>

#undef min
#undef max

#include "willpower/common/Vector2.h"
#include "willpower/common/CachedStaticAccelerationGrid.h"

namespace WP_NAMESPACE
{

	/*
	Use (instead of DynamicAccelerationGrid) when you need more speed,
	at the expense of memory.
	*/
	class WP_COMMON_API CachedDynamicAccelerationGrid : public CachedStaticAccelerationGrid
	{
		int mMoveCount;

		std::vector<uint64_t> mItemCellHashes;

	private:

		static const uint16_t EMPTY_HASH_ENTRY = 0;
		static const uint64_t EMPTY_HASH_SET = 0;
		static const int SET_BITS = 16;
		static const int MAX_SIZE_MASK = (1 << SET_BITS) - 1;

	private:

		void addItem(uint32_t itemId, uint64_t hash);

		uint64_t getHash(Vector2 const& minExtent, Vector2 const& maxExtent) const;

		uint64_t getHash(int x0, int y0, int x1, int y1) const;

	public:

		CachedDynamicAccelerationGrid(Vector2 const& offset, Vector2 const& size, int cellDimX, int cellDimY, int initialCount, float maxItemSize);

		CachedDynamicAccelerationGrid(float x, float y, float sizeX, float sizeY, int cellDimX, int cellDimY, int initialCount, float maxItemSize);

		int getMoveCount() const;

		void resetMoveCount();

		void addItem(uint32_t itemId, Vector2 const& minExtent, Vector2 const& maxExtent, NarrowPhaseFunction narrowFn = PassThrough);

		void removeItem(uint32_t itemId);

		void moveItem(uint32_t itemId, Vector2 const& minExtent, Vector2 const& maxExtent);

		void moveItem(uint32_t itemId, BoundingBox const& bounds);

	};

} // WP_NAMESPACE
