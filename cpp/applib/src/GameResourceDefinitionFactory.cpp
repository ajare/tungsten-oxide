#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/common/Exceptions.h"

#include "GameResourceDefinitionFactory.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	GameResourceDefinitionFactory::GameResourceDefinitionFactory(string const& factoryType)
		: ResourceDefinitionFactory("Game", factoryType)
	{
	}

} // applib