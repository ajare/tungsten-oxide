#include "willpower/application/resourcesystem/DataStream.h"
#include "willpower/application/resourcesystem/ResourceLocation.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			DataStream::DataStream(ResourceLocation* location, string const& source, string const& namesp)
				: mData(nullptr)
				, mSize(0)
				, mwLocation(location)
				, mSource(source)
				, mNamespace(namesp)
			{
			}

			DataStream::DataStream(uint8_t* data, uint32_t size)
				: mData(data)
				, mSize(size)
				, mwLocation(nullptr)
			{
			}

			DataStream::~DataStream()
			{
				deleteData();
			}

			void DataStream::deleteData()
			{
				delete[] mData;
				mData = nullptr;
				mSize = 0;
			}

			uint8_t const* DataStream::getData() const
			{
				return mData;
			}

			uint32_t DataStream::getSize() const
			{
				return mSize;
			}

			string const& DataStream::getSource() const
			{
				return mSource;
			}

			string const& DataStream::getNamespace() const
			{
				return mNamespace;
			}

			void DataStream::read()
			{
				if (mwLocation)
				{
					deleteData();
					mData = mwLocation->readData(mSource, &mSize);
				}
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE