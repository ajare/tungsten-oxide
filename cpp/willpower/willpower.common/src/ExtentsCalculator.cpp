#include "willpower/common/ExtentsCalculator.h"

using namespace std;

namespace WP_NAMESPACE
{
	ExtentsCalculator::ExtentsCalculator(Vector2 const& minExtent, Vector2 const& maxExtent, float padding)
		: mMinExtent(minExtent)
		, mMaxExtent(maxExtent)
		, mPadding(padding)
	{
	}

	Vector2 ExtentsCalculator::getMinExtent() const
	{
		return mMinExtent - Vector2(mPadding, mPadding);
	}

	Vector2 ExtentsCalculator::getMaxExtent() const
	{
		return mMaxExtent + Vector2(mPadding, mPadding);
	}

	Vector2 ExtentsCalculator::getSize() const
	{
		return getMaxExtent() - getMinExtent();
	}

	Vector2 ExtentsCalculator::getCentre() const
	{
		return mMinExtent + (mMaxExtent - mMinExtent) / 2.0f;
	}

	Vector2 ExtentsCalculator::getCellSize(uint32_t numCellsX, uint32_t numCellsY) const
	{
		return getSize() / Vector2((float)numCellsX, (float)numCellsY);
	}

} // WP_NAMESPACE
