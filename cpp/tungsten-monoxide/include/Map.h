#pragma once

#include <string>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

#include <willpower/common/Logger.h>

#include <applib/Map.h>

class Map : public applib::Map
{
	wp::Logger* mwLogger;

public:

	Map(std::string const& name,
		std::string const& namesp,
		std::string const& source,
		std::map<std::string, std::string> const& tags,
		wp::application::resourcesystem::ResourceLocation* location,
		wp::Logger* logger
	);

	~Map();
};

class MapResourceFactory : public wp::application::resourcesystem::ResourceFactory
{
	wp::Logger* mwLogger;

public:

	explicit MapResourceFactory(wp::Logger* logger)
		: wp::application::resourcesystem::ResourceFactory("Track")
		, mwLogger(logger)
	{
	}

	wp::application::resourcesystem::Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, wp::application::resourcesystem::ResourceLocation* location) override
	{
		return new Map(name, namesp, source, tags, location, mwLogger);
	}
};