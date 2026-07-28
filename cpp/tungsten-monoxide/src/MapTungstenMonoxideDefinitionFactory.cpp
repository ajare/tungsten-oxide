#include <willpower/application/resourcesystem/ResourceManager.h>

#include "MapTungstenMonoxideDefinitionFactory.h"
#include "Map.h"


MapTungstenMonoxideDefinitionFactory::MapTungstenMonoxideDefinitionFactory()
	: applib::MapResourceDefinitionFactory("Track")
{
}

// Reads the .mppmodel filename out of <Definition factory="Track"><File>...</File></Definition>
// and stashes it on Map::mModelFileName, for Map::load() to resolve against the resource's
// DirectoryResourceLocation. This can't just be the Resource's own `location`/getSource(): Track
// is a composite resource now (it lists TrackMaterial dependents), and
// ResourceManager::instantiateResource() unconditionally forces a composite resource's `source` to
// "" regardless of any `location=` attribute -- see Map.h's comment on mModelFileName.
void MapTungstenMonoxideDefinitionFactory::create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::XmlNode* node)
{
	VAR_UNUSED(resourceMgr);

	auto mapRes = static_cast<Map*>(resource);

	auto fileNode = node->getChild("File");
	mapRes->mModelFileName = fileNode->getValue();
}
