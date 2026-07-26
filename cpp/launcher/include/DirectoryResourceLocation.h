#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>

#include "willpower/application/resourcesystem/ResourceLocation.h"

class DirectoryResourceLocation : public wp::application::resourcesystem::ResourceLocation
{
	std::string mRootPath, mDefinitionPath;

private:

	/*
	wp::application::resourcesystem::DataStreamPtr getHardResourceDataStream(std::string const& file, std::string const& namesp) const;

	wp::application::resourcesystem::DataStreamPtr getHardResourceDataStreamProgressive(std::string const& file, std::string const& namesp, DataStreamFetchProgressCallback progress) const;
	*/

	bool hardResourceExists(std::string const& file) const;

public:

	DirectoryResourceLocation(wp::Logger* logger, std::string const& directory, std::string const& definitionFile);

	std::string const& getRootPath() const;

	uint8_t* readData(std::string const& source, uint32_t* dataSize);

	std::string const& getDefinitionFile() const;
};
