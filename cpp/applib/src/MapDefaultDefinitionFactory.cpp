#include <utils/StringUtils.h>

#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/common/Exceptions.h"

#include "Map.h"
#include "MapDefaultDefinitionFactory.h"
#include "MapGeometryObjectAttributes.h"

namespace applib
{
	using namespace std;
	using namespace utils;
	using namespace wp;

	MapDefaultDefinitionFactory::MapDefaultDefinitionFactory()
		: MapResourceDefinitionFactory("")
	{
	}

	void MapDefaultDefinitionFactory::create(application::resourcesystem::Resource* resource, application::resourcesystem::ResourceManager* resourceMgr, XmlNode* node)
	{
	}

} // applib
