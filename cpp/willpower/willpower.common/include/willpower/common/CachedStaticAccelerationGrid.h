#pragma once

#include <vector>
#include <map>
#include <set>
#include <functional>

#undef min
#undef max

#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingBox.h"
#include "willpower/common/BoundingCircle.h"
#include "willpower/common/Renderable.h"

namespace WP_NAMESPACE
{

	/*
	Use when the items in the grid are static.
	*/
	class WP_COMMON_API CachedStaticAccelerationGrid : public Renderable
	{
	public:

		typedef std::set<uint32_t> IndexCollection;

		typedef std::function<bool(Vector2 const&, Vector2 const&)> NarrowPhaseFunction;

	private:

		mutable std::vector<uint32_t> mCache;

		// OPTIMISE:
		// Depending on grid size and item count, could use a vector<uint64_t> or
		// even a bitfield.
		//mutable std::map<uint64_t, uint64_t> mCacheEntries;
		mutable std::vector<uint64_t> mCacheEntries;

		int mDimBitsX, mDimBitsY;

		mutable uint64_t mCacheEnd;
		mutable int mCacheHits, mCacheMisses;

	protected:

		Vector2 mOffset;

		Vector2 mSize, mCellSize;

		int mCellDimX, mCellDimY;

		std::vector<IndexCollection> mCells;

	protected:

		static NarrowPhaseFunction PassThrough;

	private:

		uint64_t getCacheHash(Vector2 const& minExtent, Vector2 const& maxExtent) const;

		uint64_t getCacheHash(uint64_t x0, uint64_t y0, uint64_t x1, uint64_t y1) const;

		uint32_t const* getItemsInArea(Vector2 const& minExtent, Vector2 const& maxExtent, uint32_t const** end) const;

	public:

		CachedStaticAccelerationGrid(Vector2 const& offset, Vector2 const& size, int cellDimX, int cellDimY, int initialCount);

		CachedStaticAccelerationGrid(float x, float y, float sizeX, float sizeY, int cellDimX, int cellDimY, int initialCount);

		Vector2 const& getOffset() const;

		Vector2 const& getSize() const;

		int getCellDimensionX() const;

		int getCellDimensionY() const;

		Vector2 const& getCellSize() const;

		void clearCache();

		int getCacheHits() const;

		int getCacheMisses() const;

		uint32_t const* getCandidateItemsInBoundingArea(BoundingCircle const& area, uint32_t const** end) const;

		uint32_t const* getCandidateItemsInBoundingArea(BoundingBox const& area, uint32_t const** end) const;

		uint32_t const* _getItemsInCellRange(int x0, int y0, int x1, int y1, uint32_t const** end) const;

		virtual void addItem(uint32_t itemId, Vector2 const& minExtent, Vector2 const& maxExtent, NarrowPhaseFunction narrowFn = PassThrough);

		void addItem(uint32_t itemId, BoundingBox const& bounds, NarrowPhaseFunction narrowFn = PassThrough);

	};

} // WP_NAMESPACE
