#include <willpower/application/resourcesystem/ResourceManager.h>

#include "MapTungstenMonoxideDefinitionFactory.h"
#include "Map.h"


MapTungstenMonoxideDefinitionFactory::MapTungstenMonoxideDefinitionFactory()
	: applib::MapResourceDefinitionFactory("Track")
{
}

void MapTungstenMonoxideDefinitionFactory::create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::XmlNode* node)
{
	VAR_UNUSED(resourceMgr);
	VAR_UNUSED(node);

	auto mapRes = static_cast<Map*>(resource);

	auto resourceNode = node->getChild("Resource");
	auto resourceName = resourceNode->getValue();

	auto depResource = mapRes->getDependentResource(resourceName);

	if (resourceName == "Yaml")
	{
	}
}
