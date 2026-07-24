#pragma once

#include <vector>
#include <set>
#include <unordered_set>
#include <map>

#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingBox.h"
#include "willpower/common/BoundingCircle.h"

namespace WP_NAMESPACE
{

	/*
	Use when you need a grid with no restrictions (ie on number of cells,
	or or maximum size of objects).
	*/
	class WP_COMMON_API AccelerationGrid
	{
	public:

		typedef std::set<uint32_t> IndexCollection;

	private:

		// Map index to a set of cell indices that it is in.
		std::map<uint32_t, IndexCollection> mIndicesToCells;

	protected:

		Vector2 mOffset;

		Vector2 mSize;

		int mCellDimX, mCellDimY;

		std::vector<IndexCollection> mCells;

		int mMoveCount;

	private:

		// Helper functions
		IndexCollection& getCellItems(int x, int y);

		IndexCollection getItemsInArea(Vector2 const& minExtent, Vector2 const& maxExtent) const;

		bool cellHasItem(IndexCollection const& cell, uint32_t index) const;

		void addItemToCell(IndexCollection& cell, uint32_t index);

		void removeItemFromCell(IndexCollection& cell, uint32_t index);

	public:

		AccelerationGrid(Vector2 const& offset, Vector2 const& size, int cellDimX, int cellDimY, float padding = 0.001f);

		AccelerationGrid(float x, float y, float sizeX, float sizeY, int cellDimX, int cellDimY, float padding = 0.001f);

		Vector2 const& getOffset() const;

		Vector2 const& getSize() const;

		int getCellDimensionX() const;

		int getCellDimensionY() const;

		Vector2 getCellSize() const;

		void clear();

		IndexCollection const& _getCellItems(int x, int y) const;

		void addItem(uint32_t index, BoundingBox const& box);

		void removeItem(uint32_t index);

		void removeAllItems();

		void moveItem(uint32_t index, BoundingBox const& newBox);

		int getMoveCount() const;

		void resetMoveCount();

		int getCount(int cellX, int cellY) const;

		void getContainingCell(bool checkBounds, float x, float y, int& cellX, int& cellY) const;

		void getCellExtents(int cellX, int cellY, Vector2& minExtent, Vector2& maxExtent);

		template<typename A>
		IndexCollection getCandidateItemsInBoundingArea(A const& area) const
		{
			Vector2 minExtent, maxExtent;
			area.getExtents(minExtent, maxExtent);

			return getItemsInArea(minExtent, maxExtent);
		}

		IndexCollection _getItemsInCellRange(int x0, int y0, int x1, int y1) const;

		IndexCollection const& _getItemCellIndices(uint32_t index) const;
	};

} // WP_NAMESPACE
