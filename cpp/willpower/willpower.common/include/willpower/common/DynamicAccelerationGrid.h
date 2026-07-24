#pragma once

#include <vector>
#include <map>
#include <set>

#undef min
#undef max

#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingBox.h"
#include "willpower/common/BoundingCircle.h"
#include "willpower/common/Renderable.h"

namespace WP_NAMESPACE
{

	/*
	Use when objects in the grid are moving a lot, and are small enough
	that they can fit into a maximum of 2x2 grid cells.
	*/
	class WP_COMMON_API DynamicAccelerationGrid : public Renderable
	{
	public:

		typedef std::set<uint32_t> IndexCollection;

	private:

		Vector2 mOffset;

		Vector2 mSize, mCellSize;

		int mCellDimX, mCellDimY;

		int mMoveCount;

		std::vector<IndexCollection> mCells;

		std::vector<uint64_t> mItemCellHashes;

	private:

		static const uint16_t EMPTY_HASH_ENTRY	= 0;
		static const uint64_t EMPTY_HASH_SET	= 0;
		static const int SET_BITS				= 16;
		static const int MAX_SIZE_MASK			= (1 << SET_BITS) - 1;

	private:

		void addItem(uint32_t itemId, uint64_t hash);

		uint64_t getHash(Vector2 const& minExtent, Vector2 const& maxExtent) const;

		uint64_t getHash(int x0, int y0, int x1, int y1) const;

		IndexCollection getItemsInArea(Vector2 const& minExtent, Vector2 const& maxExtent) const;

	public:

		DynamicAccelerationGrid(Vector2 const& offset, Vector2 const& size, int cellDimX, int cellDimY, int initialCount, float maxItemSize);

		DynamicAccelerationGrid(float x, float y, float sizeX, float sizeY, int cellDimX, int cellDimY, int initialCount, float maxItemSize);

		Vector2 const& getOffset() const;

		Vector2 const& getSize() const;

		int getCellDimensionX() const;

		int getCellDimensionY() const;

		Vector2 const& getCellSize() const;

		int getMoveCount() const;

		void resetMoveCount();

		std::vector<uint32_t> getCandidateItemsInBoundingArea(BoundingCircle const& area) const;

		std::vector<uint32_t> getCandidateItemsInBoundingArea(BoundingBox const& area) const;

		IndexCollection _getItemsInCellRange(int x0, int y0, int x1, int y1) const;

		void addItem(uint32_t itemId, Vector2 const& minExtent, Vector2 const& maxExtent);

		void addItem(uint32_t itemId, BoundingBox const& bounds);

		void removeItem(uint32_t itemId);

		void moveItem(uint32_t itemId, Vector2 const& minExtent, Vector2 const& maxExtent);

		void moveItem(uint32_t itemId, BoundingBox const& bounds);

	};

} // WP_NAMESPACE
