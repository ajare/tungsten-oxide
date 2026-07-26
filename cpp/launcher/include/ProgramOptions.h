#pragma once

#include <string>
#include <vector>
#include <map>

#include <willpower/common/Logger.h>

#include <willpower/application/AudioOptions.h>

struct ProgramOptions
{
	struct ResourceLocation
	{
		std::string type;
		std::string path;
		std::string definitionFile;
	};

	struct Debugging
	{
		bool inGame;
	};

public:

	int screenWidth, screenHeight;

	bool fullScreen, vSync;

	std::string dll;

	std::string gameResource;

	std::vector<ResourceLocation> resourceLocations;

	std::map<std::string, std::string> arguments;

	bool audioEnabled;

	wp::application::AudioOptions audio;

	Debugging debugging;
};

ProgramOptions parseProgramOptions(std::string const& filename);

void logProgramOptions(ProgramOptions const& options, wp::Logger* logger);