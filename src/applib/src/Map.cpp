#include "willpower/application/resourcesystem/ResourceExceptions.h"

#include "Map.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	Map::Map(string const& name, 
		string const& namesp, 
		string const& source, 
		map<string, string> const& tags, 
		application::resourcesystem::ResourceLocation* location,
		float accelGridSize)
		: application::resourcesystem::Resource(name, namesp, "Map", source, tags, location)
	{
	}

	Map::~Map()
	{
	}

	void Map::create(application::resourcesystem::DataStreamPtr dataPtr, application::resourcesystem::ResourceManager* resourceMgr)
	{
		parseData(dataPtr);
		parseDefinition(resourceMgr);
	}

	void Map::destroy()
	{
	}

} // applib