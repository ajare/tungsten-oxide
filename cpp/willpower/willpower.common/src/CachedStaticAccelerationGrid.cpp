#include <cassert>
#include <iterator>
#include <algorithm>

#include "willpower/common/CachedStaticAccelerationGrid.h"
#include "willpower/common/MathsUtils.h"
#include "willpower/common/Exceptions.h"

using namespace std;

namespace WP_NAMESPACE
{
	CachedStaticAccelerationGrid::NarrowPhaseFunction CachedStaticAccelerationGrid::PassThrough
		= [](Vector2 const&, Vector2 const&)
	{
		return true;
	};

	CachedStaticAccelerationGrid::CachedStaticAccelerationGrid(Vector2 const& offset, Vector2 const& size, int cellDimX, int cellDimY, int initialCount)
		: mOffset(offset)
		, mSize(size)
		, mCellDimX(cellDimX)
		, mCellDimY(cellDimY)
		, mCacheEnd(0)
		, mCacheHits(0)
		, mCacheMisses(0)
	{
		WP_UNUSED(initialCount);

		// Extend the grid a little to avoid floating point issues
		Vector2 extend = mSize / 1000.0f;
		mOffset -= extend;
		mSize += extend * 2;

		mCellSize = Vector2(mSize.x / mCellDimX, mSize.y / mCellDimY);

		int cellCount = mCellDimX * mCellDimY;
		mCells.reserve(cellCount);
		mCells.resize(cellCount);

		// Set up cache
		mDimBitsX = MathsUtils::bitsRequired(mCellDimX - 1);
		mDimBitsY = MathsUtils::bitsRequired(mCellDimY - 1);

		uint64_t maxHashValue = getCacheHash(mCellDimX - 1, mCellDimY - 1, mCellDimX - 1, mCellDimY - 1);
		mCacheEntries.resize((size_t)maxHashValue, (uint64_t)-1);
		mCache.resize(4096);
		clearCache();
	}

	CachedStaticAccelerationGrid::CachedStaticAccelerationGrid(float x, float y, float sizeX, float sizeY, int cellDimX, int cellDimY, int initialCount)
		: CachedStaticAccelerationGrid(Vector2(x, y), Vector2(sizeX, sizeY), cellDimX, cellDimY, initialCount)
	{
	}

	Vector2 const& CachedStaticAccelerationGrid::getOffset() const
	{
		return mOffset;
	}

	Vector2 const& CachedStaticAccelerationGrid::getSize() const
	{
		return mSize;
	}

	int CachedStaticAccelerationGrid::getCellDimensionX() const
	{
		return mCellDimX;
	}

	int CachedStaticAccelerationGrid::getCellDimensionY() const
	{
		return mCellDimY;
	}

	Vector2 const& CachedStaticAccelerationGrid::getCellSize() const
	{
		return mCellSize;
	}

	void CachedStaticAccelerationGrid::clearCache()
	{
		//mCacheEntries.clear();
		mCacheEnd = 0;
		mCacheHits = 0;
		mCacheMisses = 0;
	}

	int CachedStaticAccelerationGrid::getCacheHits() const
	{
		return mCacheHits;
	}

	int CachedStaticAccelerationGrid::getCacheMisses() const
	{
		return mCacheMisses;
	}

	uint64_t CachedStaticAccelerationGrid::getCacheHash(Vector2 const& minExtent, Vector2 const& maxExtent) const
	{
		// See which cells it spans.
		Vector2 minCell = (minExtent - mOffset) / mCellSize;
		Vector2 maxCell = (maxExtent - mOffset) / mCellSize;

		uint64_t cellX0 = (uint64_t)minCell.x;
		uint64_t cellY0 = (uint64_t)minCell.y;
		uint64_t cellX1 = (uint64_t)maxCell.x;
		uint64_t cellY1 = (uint64_t)maxCell.y;

		return getCacheHash(cellX0, cellY0, cellX1, cellY1);
	}

	uint64_t CachedStaticAccelerationGrid::getCacheHash(uint64_t x0, uint64_t y0, uint64_t x1, uint64_t y1) const
	{
		//return (x0 << 0) + (y0 << 16) + (x1 << 32) + (y1 << 48);
		return (x0 << 0) + (y0 << mDimBitsX) + (x1 << (mDimBitsX + mDimBitsY)) + (y1 << (mDimBitsX + mDimBitsY + mDimBitsX));
	}

	void CachedStaticAccelerationGrid::addItem(uint32_t itemId, Vector2 const& minExtent, Vector2 const& maxExtent, NarrowPhaseFunction narrowFn)
	{
		// See which cells it spans
		Vector2 cellSize = getCellSize();
		Vector2 minCell = (minExtent - mOffset) / cellSize;
		Vector2 maxCell = (maxExtent - mOffset) / cellSize;

		int cellX0 = (int)minCell.x;
		int cellY0 = (int)minCell.y;
		int cellX1 = (int)maxCell.x;
		int cellY1 = (int)maxCell.y;

		// Clamp to the grid.  The only time it would be expected to go
		// outside is when it lies directly on an exterior gridline.
		cellX0 = max(0, min(cellX0, mCellDimX - 1));
		cellY0 = max(0, min(cellY0, mCellDimY - 1));
		cellX1 = max(0, min(cellX1, mCellDimX - 1));
		cellY1 = max(0, min(cellY1, mCellDimY - 1));

		// Add to cells, and cell-to-index map
		for (int y = cellY0; y <= cellY1; ++y)
		{
			for (int x = cellX0; x <= cellX1; ++x)
			{
				Vector2 c0 = mOffset + Vector2((float)x, (float)y) * mCellSize;
				Vector2 c1 = mOffset + Vector2((float)(x + 1), (float)(y + 1)) * mCellSize;

				if (narrowFn(c0, c1))
				{
					mCells[y * mCellDimX + x].insert(itemId);
				}
			}
		}
	}

	void CachedStaticAccelerationGrid::addItem(uint32_t itemId, BoundingBox const& bounds, NarrowPhaseFunction narrowFn)
	{
		addItem(itemId, bounds.getMinExtent(), bounds.getMaxExtent(), narrowFn);
	}
	
	uint32_t const* CachedStaticAccelerationGrid::getCandidateItemsInBoundingArea(BoundingCircle const& area, uint32_t const** end) const
	{
		Vector2 minExtent, maxExtent;
		area.getExtents(minExtent, maxExtent);

		return getItemsInArea(minExtent, maxExtent, end);
	}

	uint32_t const* CachedStaticAccelerationGrid::getCandidateItemsInBoundingArea(BoundingBox const& area, uint32_t const** const end) const
	{
		Vector2 minExtent, maxExtent;
		area.getExtents(minExtent, maxExtent);

		return getItemsInArea(minExtent, maxExtent, end);
	}

	uint32_t const* CachedStaticAccelerationGrid::getItemsInArea(Vector2 const& minExtent, Vector2 const& maxExtent, uint32_t const** end) const
	{
		int minX = (std::max)(0, (int)((minExtent.x - mOffset.x) / mCellSize.x));
		int minY = (std::max)(0, (int)((minExtent.y - mOffset.y) / mCellSize.y));
		int maxX = (std::min)((int)((maxExtent.x - mOffset.x) / mCellSize.x), mCellDimX - 1);
		int maxY = (std::min)((int)((maxExtent.y - mOffset.y) / mCellSize.y), mCellDimY - 1);

		return _getItemsInCellRange(minX, minY, maxX, maxY, end);
	}

	uint32_t const* CachedStaticAccelerationGrid::_getItemsInCellRange(int x0, int y0, int x1, int y1, uint32_t const** end) const
	{
		uint64_t hash = getCacheHash(x0, y0, x1, y1);
		uint64_t entryPos, entrySize;
		
		/*
		auto it = mCacheEntries.find(hash);

		// Get cached results
		if (it != mCacheEntries.end())
		*/
		if (mCacheEntries[(uint32_t)hash] != (uint64_t)-1)
		{
			mCacheHits++;

			//uint64_t entry = it->second;
			uint64_t entry = mCacheEntries[(uint32_t)hash];

			entryPos = entry & 0xffffffff;
			entrySize = entry >> 32;
		}
		else
		{
			mCacheMisses++;

			// Get results
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

			entryPos = mCacheEnd;
			entrySize = indices.size();

			uint64_t cacheEntry = entryPos + (entrySize << 32);

			// Insert into cache
			while (mCache.size() < (entryPos + entrySize))
			{
				mCache.resize(mCache.size() * 2);
			}

			mCacheEntries[(uint32_t)hash] = cacheEntry;
			copy(indices.begin(), indices.end(), inserter(mCache, mCache.begin() + (uint32_t)entryPos));

			// Update cache
			mCacheEnd += entrySize;
		}

		*end = &(mCache[(uint32_t)(entryPos + entrySize)]);
		return &(mCache[(uint32_t)entryPos]);
	}

} // WP_NAMESPACE
