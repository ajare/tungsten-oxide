#pragma once

#include <vector>
#include <set>
#include <unordered_set>
#include <map>
#include <algorithm>
#include <iterator>
#include <functional>
#include <format>

#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingBox.h"
#include "willpower/common/BoundingCircle.h"

namespace WP_NAMESPACE
{

	/*
	Use when you need a grid with no restrictions (ie on number of cells,
	or or maximum size of objects).
	*/
	template<typename T>
	class ExtendedAccelerationGrid
	{
	public:

		typedef std::set<uint32_t> IndexCollection;

		struct Cell
		{
			IndexCollection indices;
			T user;
		};

		typedef std::function<void(T*)> CellUserUpdateFunction;

	private:

		// Map index to a set of cell indices that it is in.
		std::map<uint32_t, IndexCollection> mIndicesToCells;

	protected:

		Vector2 mOffset;

		Vector2 mSize;

		int mCellDimX, mCellDimY;

		std::vector<Cell> mCells;

		int mMoveCount;

	private:

		// Helper functions
		Cell& getCell(int x, int y)
		{
			return mCells[y * mCellDimX + x];
		}

		IndexCollection getItemsInArea(Vector2 const& minExtent, Vector2 const& maxExtent) const
		{
			Vector2 cellSize = getCellSize();

			int minX = (std::max)(0, (int)((minExtent.x - mOffset.x) / cellSize.x));
			int minY = (std::max)(0, (int)((minExtent.y - mOffset.y) / cellSize.y));
			int maxX = (std::min)((int)((maxExtent.x - mOffset.x) / cellSize.x), mCellDimX - 1);
			int maxY = (std::min)((int)((maxExtent.y - mOffset.y) / cellSize.y), mCellDimY - 1);

			return _getItemsInCellRange(minX, minY, maxX, maxY);
		}

		bool cellHasItem(Cell const& cell, uint32_t index) const
		{
			return cell.indices.find(index) != cell.indices.end();
		}

		void addItemToCell(Cell& cell, uint32_t index, CellUserUpdateFunction updateFn)
		{
			cell.indices.insert(index);

			if (updateFn)
			{
				updateFn(&cell.user);
			}
		}

		void removeItemFromCell(Cell& cell, uint32_t index, CellUserUpdateFunction updateFn, bool failIfNotFound = true)
		{
			bool foundItem{ true };
			try
			{
				cell.indices.erase(index);
			}
			catch (std::exception& e)
			{
				foundItem = false;
				if (failIfNotFound)
				{
					throw e;
				}
			}

			if (updateFn && foundItem)
			{
				updateFn(&cell.user);
			}
		}

	public:

		ExtendedAccelerationGrid(Vector2 const& offset, Vector2 const& size, int cellDimX, int cellDimY, float padding = 0.001f)
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

		ExtendedAccelerationGrid(float x, float y, float sizeX, float sizeY, int cellDimX, int cellDimY, float padding = 0.001f)
			: ExtendedAccelerationGrid(Vector2(x, y), Vector2(sizeX, sizeY), cellDimX, cellDimY, padding)
		{
		}

		Vector2 const& getOffset() const
		{
			return mOffset;
		}

		Vector2 const& getSize() const
		{
			return mSize;
		}

		int getCellDimensionX() const
		{
			return mCellDimX;
		}

		int getCellDimensionY() const
		{
			return mCellDimY;
		}

		Vector2 getCellSize() const
		{
			return Vector2(mSize.x / mCellDimX, mSize.y / mCellDimY);
		}

		void clear()
		{
			mIndicesToCells.clear();
			mMoveCount = 0;

			for (auto& cell : mCells)
			{
				cell.indices.clear();
				cell.user = T();
			}
		}

		Cell const& getCell(int x, int y) const
		{
			return mCells[y * mCellDimX + x];
		}

		T const& getUser(int x, int y) const
		{
			return mCells[y * mCellDimX + x].user;
		}

		T& getUser(int x, int y)
		{
			return mCells[y * mCellDimX + x].user;
		}

		T const& getUser(uint32_t index) const
		{
			return mCells[index].user;
		}

		T& getUser(uint32_t index)
		{
			return mCells[index].user;
		}

		void addItem(uint32_t index, BoundingBox const& box, CellUserUpdateFunction updateFn)
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
			cellX0 = std::max(0, std::min(cellX0, mCellDimX - 1));
			cellY0 = std::max(0, std::min(cellY0, mCellDimY - 1));
			cellX1 = std::max(0, std::min(cellX1, mCellDimX - 1));
			cellY1 = std::max(0, std::min(cellY1, mCellDimY - 1));

			// Add to cells, and cell-to-index map
			mIndicesToCells[index] = IndexCollection();
			auto& indexToCellIt = mIndicesToCells[index];

			for (int y = cellY0; y <= cellY1; ++y)
			{
				for (int x = cellX0; x <= cellX1; ++x)
				{
					addItemToCell(getCell(x, y), index, updateFn);

					uint32_t cellIindex = mCellDimX * y + x;
					indexToCellIt.insert(cellIindex);
				}
			}
		}

		void addItem(uint32_t index, BoundingBox const& box)
		{
			addItem(index, box, nullptr);
		}

		void removeItem(uint32_t index, CellUserUpdateFunction updateFn, bool failIfNotFound = true)
		{
			// Find which cells the item is in, and remove it from them.
			auto it = mIndicesToCells.find(index);

			if (it == mIndicesToCells.end() && failIfNotFound)
			{
				std::string errMsg = std::format("Index {} not found in AccelerationGrid", index);
				throw std::exception(errMsg.c_str());
			}

			if (it != mIndicesToCells.end())
			{
				for (auto cellIndex : it->second)
				{
					removeItemFromCell(mCells[cellIndex], index, updateFn, failIfNotFound);
				}

				mIndicesToCells.erase(it);
			}
		}

		void removeItem(uint32_t index, bool failIfNotFound = true)
		{
			removeItem(index, nullptr, failIfNotFound);
		}

		void removeAllItems(CellUserUpdateFunction updateFn)
		{
			for (auto& cell : mCells)
			{
				cell.indices.clear();
				
				if (updateFn)
				{
					updateFn(&cell.user);
				}
			}

			mIndicesToCells.clear();
			mMoveCount = 0;
		}

		void removeAllItems()
		{
			removeAllItems(nullptr);
		}

		void moveItem(uint32_t index, BoundingBox const& newBox, CellUserUpdateFunction updateFn)
		{
			// Remove item, then add it back.
			removeItem(index, updateFn);
			addItem(index, newBox, updateFn);
			mMoveCount++;
		}

		void moveItem(uint32_t index, BoundingBox const& newBox)
		{
			moveItem(index, newBox, nullptr);
		}

		int getMoveCount() const
		{
			return mMoveCount;
		}

		void resetMoveCount()
		{
			mMoveCount = 0;
		}

		int getCount(int cellX, int cellY) const
		{
			return (int)getCell(cellX, cellY).indices.size();
		}

		void getContainingCell(bool checkBounds, float x, float y, int& cellX, int& cellY) const
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

		void getCellExtents(int cellX, int cellY, Vector2& minExtent, Vector2& maxExtent)
		{
			Vector2 cellSize = getCellSize();

			minExtent.x = mOffset.x + cellSize.x * cellX;
			minExtent.y = mOffset.y + cellSize.y * cellY;
			maxExtent = minExtent + cellSize;
		}

		template<typename A>
		IndexCollection getCandidateItemsInBoundingArea(A const& area) const
		{
			Vector2 minExtent, maxExtent;
			area.getExtents(minExtent, maxExtent);

			return getItemsInArea(minExtent, maxExtent);
		}

		IndexCollection _getItemsInCellRange(int x0, int y0, int x1, int y1) const
		{
			IndexCollection indices;

			// Add items in all cells
			for (int y = y0; y <= y1; ++y)
			{
				for (int x = x0; x <= x1; ++x)
				{
					auto const& cell = getCell(x, y);

					std::set_union(cell.indices.begin(), cell.indices.end(),
						indices.begin(), indices.end(),
						std::inserter(indices, indices.end()));
				}
			}

			return indices;
		}

		IndexCollection const& _getItemCellIndices(uint32_t index) const
		{
			return mIndicesToCells.at(index);
		}

		IndexCollection const& _getCellItemIndices(uint32_t index) const
		{
			return mCells[index].indices;
		}
	};

} // WP_NAMESPACE
