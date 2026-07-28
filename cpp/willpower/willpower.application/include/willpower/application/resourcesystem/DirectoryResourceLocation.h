#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/ResourceLocation.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API DirectoryResourceLocation : public ResourceLocation
			{
				std::string mRootPath, mDefinitionPath;

			private:

				bool hardResourceExists(std::string const& file) const;

			public:

				DirectoryResourceLocation(Logger* logger, std::string const& directory, std::string const& definitionFile);

				std::string const& getRootPath() const;

				uint8_t* readData(std::string const& source, uint32_t* dataSize);

				std::string const& getDefinitionFile() const;
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE
