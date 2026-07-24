#include <cassert>
#include <iterator>
#include <algorithm>

#include "willpower/common/AccelerationGrid.h"

using namespace std;

namespace WP_NAMESPACE
{

	AccelerationGrid::AccelerationGrid(Vector2 const& offset, Vector2 const& size, int cellDimX, int cellDimY, float padding)
		: mOffset(offset)
		, mSize(size)
		, mCellDimX(cellDimX)
		, mCellDimY(cellDimY)
		, mMoveCount(0)
	{
		int cellCount = mCellDimX * mCellDimY;
		mCells.reserve(cellCount);
		mCells.resize(cellCount);

		// Extend the grid a little to avoid floating point issues
		Vector2 extend = mSize * padding;
		mOffset -= extend;
		mSize += extend * 2;
	}

	AccelerationGrid::AccelerationGrid(float x, float y, float sizeX, float sizeY, int cellDimX, int cellDimY, float padding)
		: AccelerationGrid(Vector2(x, y), Vector2(sizeX, sizeY), cellDimX, cellDimY, padding)
	{
	}

	Vector2 const& AccelerationGrid::getOffset() const
	{
		return mOffset;
	}

	Vector2 const& AccelerationGrid::getSize() const
	{
		return mSize;
	}

	int AccelerationGrid::getCellDimensionX() const
	{
		return mCellDimX;
	}

	int AccelerationGrid::getCellDimensionY() const
	{
		return mCellDimY;
	}

	Vector2 AccelerationGrid::getCellSize() const
	{
		return Vector2(mSize.x / mCellDimX, mSize.y / mCellDimY);
	}

	AccelerationGrid::IndexCollection& AccelerationGrid::getCellItems(int x, int y)
	{
		return mCells[y * mCellDimX + x];
	}

	AccelerationGrid::IndexCollection const& AccelerationGrid::_getCellItems(int x, int y) const
	{
		return mCells[y * mCellDimX + x];
	}

	bool AccelerationGrid::cellHasItem(IndexCollection const& cell, uint32_t index) const
	{
		return cell.find(index) != cell.end();
	}

	void AccelerationGrid::clear()
	{
		mIndicesToCells.clear();
		mMoveCount = 0;

		for (auto& cell: mCells)
		{
			cell.clear();
		}
	}

	void AccelerationGrid::addItemToCell(IndexCollection& cell, uint32_t index)
	{
		cell.insert(index);
	}

	void AccelerationGrid::removeItemFromCell(IndexCollection& cell, uint32_t index)
	{
		cell.erase(index);
	}

	void AccelerationGrid::addItem(uint32_t index, BoundingBox const& box)
	{
		// Intersect bounding box with all cells, and all to each cell that
		// it hits.

		// Get box extents
		Vector2 minExtent, maxExtent;
		box.getExtents(minExtent, maxExtent);

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
		mIndicesToCells[index] = IndexCollection();
		auto& indexToCellIt = mIndicesToCells[index];

		for (int y = cellY0; y <= cellY1; ++y)
		{
			for (int x = cellX0; x <= cellX1; ++x)
			{
				addItemToCell(getCellItems(x, y), index);
					
				uint32_t cellIindex = mCellDimX * y + x;
				indexToCellIt.insert(cellIindex);
			}
		}
	}

	void AccelerationGrid::removeItem(uint32_t index)
	{
		// Find which cells the item is in, and remove it from them.
		auto it = mIndicesToCells.find(index);
		assert(it != mIndicesToCells.end() && "AccelerationGrid::removeItem() 'index' not found.");

		for (auto cellIndex: it->second)
		{
			removeItemFromCell(mCells[cellIndex], index);
		}

		mIndicesToCells.erase(it);
	}

	void AccelerationGrid::removeAllItems()
	{
		for (auto& cell : mCells)
		{
			cell.clear();
		}

		mIndicesToCells.clear();
		mMoveCount = 0;
	}

	void AccelerationGrid::moveItem(uint32_t index, BoundingBox const& newBox)
	{
		// Remove item, then add it back.
		removeItem(index);
		addItem(index, newBox);
		mMoveCount++;
	}

	int AccelerationGrid::getMoveCount() const
	{
		return mMoveCount;
	}

	void AccelerationGrid::resetMoveCount()
	{
		mMoveCount = 0;
	}

	int AccelerationGrid::getCount(int cellX, int cellY) const
	{
		return (int)_getCellItems(cellX, cellY).size();
	}

	void AccelerationGrid::getContainingCell(bool checkBounds, float x, float y, int& cellX, int& cellY) const
	{
		Vector2 cellSize = getCellSize();
		
		float dx = x - mOffset.x;
		float dy = y - mOffset.y;

		cellX = (int)(dx / cellSize.x);
		cellY = (int)(dy / cellSize.y);

		if (dx < 0.0f && cellX == 0)
		{
			cellX = -1;
		}
		if (dy < 0.0f && cellY == 0)
		{
			cellY = -1;
		}

		if (checkBounds)
		{
			if (cellX < 0 || cellX >= mCellDimX)
			{
				cellX = -1;
			}
			if (cellY < 0 || cellY >= mCellDimY)
			{
				cellY = -1;
			}
		}
	}

	void AccelerationGrid::getCellExtents(int cellX, int cellY, Vector2& minExtent, Vector2& maxExtent)
	{
		Vector2 cellSize = getCellSize();
		
		minExtent.x = mOffset.x + cellSize.x * cellX;
		minExtent.y = mOffset.y + cellSize.y * cellY;
		maxExtent = minExtent + cellSize;
	}

	AccelerationGrid::IndexCollection AccelerationGrid::getItemsInArea(Vector2 const& minExtent, Vector2 const& maxExtent) const
	{
		Vector2 cellSize = getCellSize();

		int minX = (std::max)(0, (int)((minExtent.x - mOffset.x) / cellSize.x));
		int minY = (std::max)(0, (int)((minExtent.y - mOffset.y) / cellSize.y));
		int maxX = (std::min)((int)((maxExtent.x - mOffset.x) / cellSize.x), mCellDimX - 1);
		int maxY = (std::min)((int)((maxExtent.y - mOffset.y) / cellSize.y), mCellDimY - 1);

		return _getItemsInCellRange(minX, minY, maxX, maxY);
	}

	AccelerationGrid::IndexCollection AccelerationGrid::_getItemsInCellRange(int x0, int y0, int x1, int y1) const
	{
		IndexCollection indices;

		// Add items in all cells
		for (int y = y0; y <= y1; ++y)
		{
			for (int x = x0; x <= x1; ++x)
			{
				auto const& cell = _getCellItems(x, y);

				set_union(cell.begin(), cell.end(),
					indices.begin(), indices.end(),
					inserter(indices, indices.end()));
			}
		}

		return indices;
	}

	AccelerationGrid::IndexCollection const& AccelerationGrid::_getItemCellIndices(uint32_t index) const
	{
		return mIndicesToCells.at(index);
	}

} // WP_NAMESPACE
