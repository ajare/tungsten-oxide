#pragma once

#include <memory>
#include <string>


#include "willpower/application/Platform.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class ResourceLocation;

			class WP_APPLICATION_API DataStream
			{
				uint8_t* mData;

				uint32_t mSize;

				ResourceLocation* mwLocation;

				std::string mSource;

				std::string mNamespace;

			private:

				void deleteData();

			public:

				DataStream(ResourceLocation* location, std::string const& source, std::string const& namesp);

				DataStream(uint8_t* data, uint32_t size);

				virtual ~DataStream();

				uint8_t const* getData() const;

				uint32_t getSize() const;

				std::string const& getSource() const;

				std::string const& getNamespace() const;

				void read();
			};

			typedef std::shared_ptr<DataStream> DataStreamPtr;

		} // resourcesystem
	} // application
} // WP_NAMESPACE

