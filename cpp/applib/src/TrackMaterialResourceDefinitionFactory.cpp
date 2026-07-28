#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/common/Exceptions.h"

#include "TrackMaterialResourceDefinitionFactory.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	TrackMaterialResourceDefinitionFactory::TrackMaterialResourceDefinitionFactory(string const& factoryType)
		: ResourceDefinitionFactory("TrackMaterial", factoryType)
	{
	}

} // applib
