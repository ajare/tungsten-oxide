#include <stack>
#include <algorithm>
#include <format>

#include "utils/StringUtils.h"
#include "utils/XmlReader.h"

#include "Platform.h"

#if APP_PLATFORM == APP_PLATFORM_WINDOWS
#   include <windows.h>
#endif

#include "ProgramOptions.h"

using namespace std;
using namespace wp;

ProgramOptions parseProgramOptions(string const& filename)
{
	utils::XmlReader* reader = utils::XmlReader::fromFile(filename);

	ProgramOptions pOpts;

	auto videoNode = reader->getNode("Configuration/Video");
	auto gameNode = reader->getNode("Configuration/Game");
	auto audioNode = reader->getNode("Configuration/Audio");

	pOpts.screenWidth = utils::StringUtils::parseInt(videoNode->getChild("Width")->getValue());
	pOpts.screenHeight = utils::StringUtils::parseInt(videoNode->getChild("Height")->getValue());

	string fullScreenStr = videoNode->getChild("Fullscreen")->getValue();
	string vsyncStr = videoNode->getChild("VSync")->getValue();

	pOpts.fullScreen = fullScreenStr == "true" || fullScreenStr == "yes";
	pOpts.vSync = vsyncStr == "true" || fullScreenStr == "yes";

	// Get game DLL
	pOpts.dll = gameNode->getChild("DLL")->getAttribute("path");

	// Game resource
	auto gameResNode = gameNode->getChild("GameResource");
	string gameRes, gameResNamespace;

	gameRes = gameResNode->getValue();
	gameResNode->getOptionalAttribute("namespace", gameResNamespace);
	pOpts.gameResource = gameResNamespace + "/" + gameRes;

	// Get game resource locations
	auto resourceLocationNode = gameNode->getChild("ResourceLocations")->getChild("ResourceLocation");
	do
	{
		ProgramOptions::ResourceLocation rl;

		rl.type = resourceLocationNode->getAttribute("type");
		rl.path = resourceLocationNode->getAttribute("path");
		rl.definitionFile = resourceLocationNode->getAttribute("definition");

		pOpts.resourceLocations.push_back(rl);
	} while (resourceLocationNode->next());

	// Get audio options
	pOpts.audioEnabled = utils::StringUtils::parseBool(audioNode->getChild("Enabled")->getValue());
	pOpts.audio.numChannels = utils::StringUtils::parseInt(audioNode->getChild("Channels")->getValue());
	pOpts.audio.synchronous = utils::StringUtils::parseBool(audioNode->getChild("Sync")->getValue());

	// Get debug options
	pOpts.debugging.inGame = false;

	auto debugNode = gameNode->getOptionalChild("Debug");
	if (debugNode)
	{
		auto inGameNode = debugNode->getOptionalChild("InGame");
		if (inGameNode)
		{
			string enabled = inGameNode->getValue();
			transform(enabled.begin(), enabled.end(), enabled.begin(), ::tolower);

			if (enabled != "enabled" && enabled != "disabled")
			{
				string errMsg = "Could not load '" + filename + "'.  Value of /Configuration/Game/Debug/InGame must be either 'enabled' or 'disabled'.";
				throw exception(errMsg.c_str());
			}

			pOpts.debugging.inGame = enabled == "enabled";
		}
	}

	// Get arguments
	auto argumentsNode = gameNode->getOptionalChild("Arguments");
	if (argumentsNode)
	{
		auto argumentNode = argumentsNode->getOptionalChild("Argument");
		while (argumentNode)
		{
			string argumentName = argumentNode->getAttribute("name");
			string argumentValue = argumentNode->getValue();
			
			pOpts.arguments[argumentName] = argumentValue;
			
			if (!argumentNode->next())
			{
				break;
			}
		}
	}

	delete reader;
	return pOpts;
}

void logProgramOptions(ProgramOptions const& options, Logger* logger)
{
	logger->info("");
	logger->info("Program Options");
	logger->info("---------------");
	
	logger->info(std::format("Video size: {}x{}", options.screenWidth, options.screenHeight));
	logger->info(std::format("Fullscreen: {}", options.fullScreen));
	logger->info(std::format("VSync enabled: {}", options.vSync));
	
	logger->info(std::format("Audio enabled: {}", options.audioEnabled));
	
	if (options.audioEnabled)
	{
		logger->info(std::format("Audio synchronous: {}", options.audio.synchronous));
		logger->info(std::format("Audio channels: {}", options.audio.numChannels));
	}

	logger->info(std::format("DLL: {}", options.dll));
	logger->info("");
	logger->info("Resource locations:");

	for (auto const& location: options.resourceLocations)
	{
		logger->info("- " + location.type + ": " + location.path);
	}

	logger->info("");
	logger->info("Debugging:");
	logger->info(std::format("- In-game: {}", options.debugging.inGame));
	logger->info("");
}