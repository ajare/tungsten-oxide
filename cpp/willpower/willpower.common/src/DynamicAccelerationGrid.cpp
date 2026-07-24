#include <cassert>
#include <iterator>
#include <algorithm>

#include "willpower/common/DynamicAccelerationGrid.h"
#include "willpower/common/Exceptions.h"

using namespace std;

namespace WP_NAMESPACE
{

	DynamicAccelerationGrid::DynamicAccelerationGrid(Vector2 const& offset, Vector2 const& size, int cellDimX, int cellDimY, int initialCount, float maxItemSize)
		: mOffset(offset)
		, mSize(size)
		, mCellDimX(cellDimX)
		, mCellDimY(cellDimY)
		, mMoveCount(0)
	{
		if (mCellDimX * mCellDimY > MAX_SIZE_MASK)
		{
			throw Exception("DynamicAccelerationGrid: cell dimensions are too large.");
		}

		// Extend the grid a little to avoid floating point issues
		Vector2 extend = mSize / 1000.0f;
		mOffset -= extend;
		mSize += extend * 2;

		mCellSize = Vector2(mSize.x / mCellDimX, mSize.y / mCellDimY);
		if (maxItemSize >= min(mCellSize.x, mCellSize.y))
		{
			throw Exception("DynamicAccelerationGrid: max item size is too large.");
		}

		int cellCount = mCellDimX * mCellDimY;
		mCells.reserve(cellCount);
		mCells.resize(cellCount);

		// Set up hashes
		mItemCellHashes.resize(initialCount, EMPTY_HASH_SET);
	}

	DynamicAccelerationGrid::DynamicAccelerationGrid(float x, float y, float sizeX, float sizeY, int cellDimX, int cellDimY, int initialCount, float maxItemSize)
		: DynamicAccelerationGrid(Vector2(x, y), Vector2(sizeX, sizeY), cellDimX, cellDimY, initialCount, maxItemSize)
	{
	}

	Vector2 const& DynamicAccelerationGrid::getOffset() const
	{
		return mOffset;
	}

	Vector2 const& DynamicAccelerationGrid::getSize() const
	{
		return mSize;
	}

	int DynamicAccelerationGrid::getCellDimensionX() const
	{
		return mCellDimX;
	}

	int DynamicAccelerationGrid::getCellDimensionY() const
	{
		return mCellDimY;
	}

	Vector2 const& DynamicAccelerationGrid::getCellSize() const
	{
		return mCellSize;
	}

	int DynamicAccelerationGrid::getMoveCount() const
	{
		return mMoveCount;
	}

	void DynamicAccelerationGrid::resetMoveCount()
	{
		mMoveCount = 0;
	}

	uint64_t DynamicAccelerationGrid::getHash(Vector2 const& minExtent, Vector2 const& maxExtent) const
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

	uint64_t DynamicAccelerationGrid::getHash(int x0, int y0, int x1, int y1) const
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
	
	void DynamicAccelerationGrid::addItem(uint32_t itemId, Vector2 const& minExtent, Vector2 const& maxExtent)
	{
		addItem(itemId, getHash(minExtent, maxExtent));
	}

	void DynamicAccelerationGrid::addItem(uint32_t itemId, BoundingBox const& bounds)
	{
		addItem(itemId, getHash(bounds.getMinExtent(), bounds.getMaxExtent()));
	}

	void DynamicAccelerationGrid::addItem(uint32_t itemId, uint64_t hash)
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

	void DynamicAccelerationGrid::removeItem(uint32_t itemId)
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
			mCells[(uint32_t)index].erase(itemId);
			hash >>= SET_BITS;
		}
	}

	void DynamicAccelerationGrid::moveItem(uint32_t itemId, Vector2 const& minExtent, Vector2 const& maxExtent)
	{
		uint64_t hash = getHash(minExtent, maxExtent);

		if (hash != mItemCellHashes[itemId])
		{
			removeItem(itemId);
			addItem(itemId, hash);
			
			mMoveCount++;
		}
	}

	void DynamicAccelerationGrid::moveItem(uint32_t itemId, BoundingBox const& bounds)
	{
		moveItem(itemId, bounds.getMinExtent(), bounds.getMaxExtent());
	}

	vector<uint32_t> DynamicAccelerationGrid::getCandidateItemsInBoundingArea(BoundingCircle const& area) const
	{
		Vector2 minExtent, maxExtent;
		area.getExtents(minExtent, maxExtent);

		IndexCollection indices = getItemsInArea(minExtent, maxExtent);
		return vector<uint32_t>(indices.begin(), indices.end());
	}

	vector<uint32_t> DynamicAccelerationGrid::getCandidateItemsInBoundingArea(BoundingBox const& area) const
	{
		Vector2 minExtent, maxExtent;
		area.getExtents(minExtent, maxExtent);

		IndexCollection indices = getItemsInArea(minExtent, maxExtent);
		return vector<uint32_t>(indices.begin(), indices.end());
	}

	DynamicAccelerationGrid::IndexCollection DynamicAccelerationGrid::getItemsInArea(Vector2 const& minExtent, Vector2 const& maxExtent) const
	{
		int minX = (std::max)(0, (int)((minExtent.x - mOffset.x) / mCellSize.x));
		int minY = (std::max)(0, (int)((minExtent.y - mOffset.y) / mCellSize.y));
		int maxX = (std::min)((int)((maxExtent.x - mOffset.x) / mCellSize.x), mCellDimX - 1);
		int maxY = (std::min)((int)((maxExtent.y - mOffset.y) / mCellSize.y), mCellDimY - 1);

		return _getItemsInCellRange(minX, minY, maxX, maxY);
	}

	DynamicAccelerationGrid::IndexCollection DynamicAccelerationGrid::_getItemsInCellRange(int x0, int y0, int x1, int y1) const
	{
		IndexCollection indices;

		for (int y = y0; y <= y1; ++y)
		{
			for (int x = x0; x <= x1; ++x)
			{
				int index = y * mCellDimX + x;
				auto const& cell = mCells[index];
				set_union(cell.begin(), cell.end(), indices.begin(), indices.end(), inserter(indices, indices.end()));
			}
		}

		return indices;
	}

} // WP_NAMESPACE
