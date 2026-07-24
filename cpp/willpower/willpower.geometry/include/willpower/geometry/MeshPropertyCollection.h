#pragma once

#include <vector>

#include "willpower/common/WillpowerWalker.h"

#include "willpower/serialization/SerializerChunk.h"
#include "willpower/serialization/SerializationUtils.h"

#include "willpower/geometry/Platform.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		template<class T>
		class MeshPropertyCollection : public serialization::SerializerChunk
		{
			std::vector<T*> mData;

		private:

			virtual T* createItem(T const* prototype) = 0;

			virtual void writeItemBinary(T const* item, std::ostream& fp) = 0;

			virtual T* readItemBinary(std::istream& fp) = 0;

			virtual void writeItemText(T const* item, std::ostream& fp) = 0;

			virtual T* readItemText(std::istream& fp) = 0;

		protected:

			void writeBinaryImpl(std::ostream& fp)
			{
				// Write each entry, terminate with -1
				for (uint32_t i = 0; i < mData.size(); ++i)
				{
					auto const* data = getItem(i);

					if (data)
					{
						serialization::SerializationUtils::writeInt32(fp, (int32_t)i);
						writeItemBinary(data, fp);
					}
				}

				serialization::SerializationUtils::writeInt32(fp, -1);
			}

			void readBinaryImpl(std::istream& fp)
			{
				int32_t index = serialization::SerializationUtils::readInt32(fp);

				while (index >= 0)
				{
					if (index >= (int32_t)mData.size())
					{
						mData.resize(index + 1, nullptr);
					}

					mData[index] = readItemBinary(fp);
					index = serialization::SerializationUtils::readInt32(fp);
				}
			}

			void writeTextImpl(std::ostream& fp)
			{
				for (uint32_t i = 0; i < mData.size(); ++i)
				{
					auto const* data = getItem(i);

					if (data)
					{
						fp << "Property item: " << i << std::endl;
						writeItemText(data, fp);
					}
				}
			}

			void readTextImpl(std::istream& fp)
			{
				WP_UNUSED(fp);
			}

			void clear()
			{
				for (auto data: mData)
				{
					delete data;
				}
				
				mData.clear();
			}

			void copyFrom(MeshPropertyCollection const& other)
			{
				clear();

				for (auto otherData : other.mData)
				{
					mData.push_back(new T(*otherData));
				}
			}

		public:

			MeshPropertyCollection(std::string const& name, std::string const& type, std::string const& desc)
				: serialization::SerializerChunk(name, type, desc)
			{
			}
			
			MeshPropertyCollection(MeshPropertyCollection const& other)
				: serialization::SerializerChunk(other.getName(), other.getType(), other.getDescription())
			{
				copyFrom(other);
			}

			virtual ~MeshPropertyCollection()
			{
				clear();
			}

			MeshPropertyCollection& operator=(MeshPropertyCollection const& other)
			{
				copyFrom(other);
				return *this;
			}

			void setItem(uint32_t index, int32_t prototype)
			{
				T* data = createItem(prototype >= 0 ? mData[prototype] : nullptr);

				ASSERT_TRACE(index <= mData.size() && "MeshPropertyCollection::setItem() 'index' does not match data entry size.");

				if (index == mData.size())
				{
					mData.push_back(data);
				}
				else
				{
					delete mData[index];
					mData[index] = data;
				}
			}

			void removeItem(uint32_t index)
			{
				delete mData[index];
				mData[index] = nullptr;
			}

			void updateItem(uint32_t oldIndex, int32_t newIndex)
			{
				delete mData[newIndex];
				mData[newIndex] = mData[oldIndex];
				mData[oldIndex] = nullptr;
			}

			T const* getItem(uint32_t index) const
			{
				return mData[index];
			}

			T* getItem(uint32_t index)
			{
				return mData[index];
			}

		};

	} // geometry
} // WP_NAMESPACE
