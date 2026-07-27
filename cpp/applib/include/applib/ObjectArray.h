#pragma once

#include <vector>

namespace applib
{

	template<typename T>
	class ObjectArray
	{
		std::vector<T> mObjects;

		std::vector<uint32_t> mFreeIndices;

	private:

		void resize(size_t newSize)
		{
			auto curSize = mObjects.size();
			if (newSize > curSize)
			{
				auto tgtSize = curSize;
				while (tgtSize < newSize)
				{
					tgtSize *= 3;
					tgtSize /= 2;
					tgtSize += 8;
				}

				mObjects.resize(tgtSize);

				// Add to free list: cast to signed int to catch curSize==0!
				for (int i = (int)tgtSize - 1; i >= (int)curSize; --i)
				{
					mFreeIndices.push_back((uint32_t)i);
				}
			}
		}

	public:

		explicit ObjectArray(size_t initialSize)
		{
			resize(initialSize);
		}

		uint32_t acquireFreeSlot()
		{
			if (mFreeIndices.empty())
			{
				resize(mObjects.size() * 2);
			}

			auto index = mFreeIndices.back();
			mFreeIndices.pop_back();
			return index;
		}

		int32_t cloneSlot(int32_t slot)
		{
			if (slot >= 0)
			{
				auto newSlot = acquireFreeSlot();
				mObjects[newSlot] = mObjects[slot];
				return newSlot;
			}
			else
			{
				return -1;
			}
		}

		void releaseSlot(uint32_t slot)
		{
			mFreeIndices.push_back(slot);
		}

		T* getObject(uint32_t index)
		{
			return &mObjects[index];
		}

		T const* getObject(uint32_t index) const
		{
			return &mObjects[index];
		}
	};

} // applib