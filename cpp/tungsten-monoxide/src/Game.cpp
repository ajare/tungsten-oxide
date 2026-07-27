#include "willpower/application/resourcesystem/ResourceExceptions.h"

#include "Game.h"

using namespace std;
using namespace wp;


Game::Game(string const& name,
	string const& namesp,
	string const& source,
	map<string, string> const& tags,
	application::resourcesystem::ResourceLocation* location)
	: applib::Game(name, namesp, source, tags, location)
{
}
