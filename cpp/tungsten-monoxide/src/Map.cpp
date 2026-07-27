#include <willpower/application/resourcesystem/TextFileResource.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include "Map.h"

using namespace std;
using namespace wp;


Map::Map(string const& name,
	string const& namesp,
	string const& source,
	map<string, string> const& tags,
	application::resourcesystem::ResourceLocation* location,
	wp::Logger* logger)
	: applib::Map(name, namesp, source, tags, location, 512)
	, mwLogger(logger)
{
}

Map::~Map()
{
}
