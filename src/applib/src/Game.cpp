#include "willpower/application/resourcesystem/ResourceExceptions.h"

#include "Game.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	Game::Game(string const& name,
		string const& namesp,
		string const& source,
		map<string, string> const& tags,
		application::resourcesystem::ResourceLocation* location)
		: application::resourcesystem::Resource(name, namesp, "Game", source, tags, location)
	{
	}

	Game::~Game()
	{
	}

	void Game::create(application::resourcesystem::DataStreamPtr dataPtr, application::resourcesystem::ResourceManager* resourceMgr)
	{
		parseData(dataPtr);
		parseDefinition(resourceMgr);
	}

} // applib