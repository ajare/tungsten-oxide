#include <cassert>
#include <iterator>
#include <algorithm>

#include "willpower/common/CachedDynamicAccelerationGrid.h"
#include "willpower/common/Exceptions.h"

using namespace std;

namespace WP_NAMESPACE
{

	CachedDynamicAccelerationGrid::CachedDynamicAccelerationGrid(Vector2 const& offset, Vector2 const& size, int cellDimX, int cellDimY, int initialCount, float maxItemSize)
		: CachedStaticAccelerationGrid(offset, size, cellDimX, cellDimY, initialCount)
		, mMoveCount(0)
	{
		if (cellDimX * cellDimY > MAX_SIZE_MASK)
		{
			throw Exception("CachedAccelerationGrid: cell dimensions are too large.");
		}

		if (maxItemSize >= min(mCellSize.x, mCellSize.y))
		{
			throw Exception("CachedAccelerationGrid: max item size is too large.");
		}

		// Set up hashes
		mItemCellHashes.resize(initialCount, EMPTY_HASH_SET);
	}

	CachedDynamicAccelerationGrid::CachedDynamicAccelerationGrid(float x, float y, float sizeX, float sizeY, int cellDimX, int cellDimY, int initialCount, float maxItemSize)
		: CachedDynamicAccelerationGrid(Vector2(x, y), Vector2(sizeX, sizeY), cellDimX, cellDimY, initialCount, maxItemSize)
	{
	}

	int CachedDynamicAccelerationGrid::getMoveCount() const
	{
		return mMoveCount;
	}

	void CachedDynamicAccelerationGrid::resetMoveCount()
	{
		mMoveCount = 0;
	}
	
	uint64_t CachedDynamicAccelerationGrid::getHash(Vector2 const& minExtent, Vector2 const& maxExtent) const
	{
		// See which cells it spans.
		Vector2 minCell = (minExtent - mOffset) / mCellSize;
		Vector2 maxCell = (maxExtent - mOffset) / mCellSize;

		int cellX0 = (int)minCell.x;
		int cellY0 = (int)minCell.y;
		int cellX1 = (int)maxCell.x;
		int cellY1 = (int)maxCell.y;

		return getHash(cellX0, cellY0, cellX1, cellY1);
	}

	uint64_t CachedDynamicAccelerationGrid::getHash(int x0, int y0, int x1, int y1) const
	{
		uint64_t hash = EMPTY_HASH_SET;
		int hashShift = 0;

		// The hash is basically an ordered (numerical ascending) bitmask of 
		// cell indices.  If the maximum item size is less than the cell size, then
		// an item can be in at most 4 cells.
		for (int y = y0; y <= y1; ++y)
		{
			for (int x = x0; x <= x1; ++x)
			{
				uint64_t index = y * mCellDimX + x;
				index += 1; // To use 0 as the empty index, making hashing quicker

				index <<= hashShift;

				hash |= index;
				hashShift += SET_BITS;
			}
		}

		return hash;
	}

	void CachedDynamicAccelerationGrid::addItem(uint32_t itemId, Vector2 const& minExtent, Vector2 const& maxExtent, NarrowPhaseFunction narrowFn)
	{
		addItem(itemId, getHash(minExtent, maxExtent));
	}

	void CachedDynamicAccelerationGrid::addItem(uint32_t itemId, uint64_t hash)
	{
		if (itemId >= mItemCellHashes.size())
		{
			mItemCellHashes.resize(mItemCellHashes.size() * 2);
		}

		mItemCellHashes[itemId] = hash;

		// Iterate over hash and add.  Stop once we hit empty.
		for (int i = 0; i < 4; ++i)
		{
			uint16_t index = (hash & MAX_SIZE_MASK);
			if (index == EMPTY_HASH_ENTRY)
			{
				break;
			}

			index--;
			mCells[index].insert(itemId);
			hash >>= SET_BITS;
		}
	}

	void CachedDynamicAccelerationGrid::removeItem(uint32_t itemId)
	{
		uint64_t hash = mItemCellHashes[itemId];
		mItemCellHashes[itemId] = EMPTY_HASH_SET;

		for (int i = 0; i < 4; ++i)
		{
			uint64_t index = (hash & MAX_SIZE_MASK);
			if (index == EMPTY_HASH_ENTRY)
			{
				break;
			}

			index--;
			mCells[(uint32_t)index].erase((uint64_t)itemId);
			hash >>= SET_BITS;
		}
	}

	void CachedDynamicAccelerationGrid::moveItem(uint32_t itemId, Vector2 const& minExtent, Vector2 const& maxExtent)
	{
		uint64_t hash = getHash(minExtent, maxExtent);

		if (hash != mItemCellHashes[itemId])
		{
			removeItem(itemId);
			addItem(itemId, hash);

			mMoveCount++;
		}
	}

	void CachedDynamicAccelerationGrid::moveItem(uint32_t itemId, BoundingBox const& bounds)
	{
		moveItem(itemId, bounds.getMinExtent(), bounds.getMaxExtent());
	}

} // WP_NAMESPACE
