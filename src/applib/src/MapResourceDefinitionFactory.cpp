#include "willpower/application/resourcesystem/ResourceExceptions.h"

#include "MapResourceDefinitionFactory.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	MapResourceDefinitionFactory::MapResourceDefinitionFactory(string const& factoryType)
		: ResourceDefinitionFactory("Map", factoryType)
	{
	}

	MapResourceDefinitionFactory::~MapResourceDefinitionFactory()
	{
	}

} // applib