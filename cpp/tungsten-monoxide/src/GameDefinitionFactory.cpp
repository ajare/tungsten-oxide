#include <map>

#include "GameDefinitionFactory.h"
#include "GameException.h"

using namespace std;

GameDefinitionFactory::GameDefinitionFactory()
	: applib::GameDefaultDefinitionFactory()
{
}

void GameDefinitionFactory::create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::XmlNode* node)
{
	VAR_UNUSED(resource);
	VAR_UNUSED(resourceMgr);
	VAR_UNUSED(node);
}