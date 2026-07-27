#include <willpower/application/resourcesystem/ResourceManager.h>

#include "MapBooleanWorldDefinitionFactory.h"
#include "Map.h"


MapBooleanWorldDefinitionFactory::MapBooleanWorldDefinitionFactory()
	: applib::MapResourceDefinitionFactory("BooleanWorld", nullptr, nullptr, nullptr, nullptr)
{
}

void MapBooleanWorldDefinitionFactory::create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, utils::XmlNode* node)
{
	VAR_UNUSED(resourceMgr);
	VAR_UNUSED(node);

	auto mapRes = static_cast<Map*>(resource);

	auto resourceNode = node->getChild("Resource");
	auto resourceName = resourceNode->getValue();

	auto depResource = mapRes->getDependentResource(resourceName);

	if (resourceName == "Yaml")
	{
		mapRes->loadWorldFromYaml(depResource);
	}
}
