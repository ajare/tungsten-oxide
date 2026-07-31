#include <set>

#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include "MapTungstenMonoxideDefinitionFactory.h"
#include "Map.h"

MapTungstenMonoxideDefinitionFactory::MapTungstenMonoxideDefinitionFactory()
    : applib::MapResourceDefinitionFactory("Track") {
}

void MapTungstenMonoxideDefinitionFactory::create(
    wp::application::resourcesystem::Resource* resource,
    wp::application::resourcesystem::ResourceManager* resourceMgr,
    wp::XmlNode* node) {
  VAR_UNUSED(resourceMgr);
  auto mapRes = static_cast<Map*>(resource);

  mapRes->mModelFileName = node->getChild("ModelFile")->getValue();
  mapRes->mTrackDataFileName = node->getChild("TrackData")->getValue();
  if (mapRes->mModelFileName.empty() || mapRes->mTrackDataFileName.empty()) {
    throw wp::application::resourcesystem::ResourceException(
        resource, "Track definition requires non-empty <ModelFile> and <TrackData> values.");
  }

  auto meshesNode = node->getChild("TrackMeshes");
  auto meshNode = meshesNode->getOptionalChild("Mesh");
  std::set<std::string> seen;
  if (meshNode) {
    do {
      std::string name = meshNode->getValue();
      if (name.empty())
        throw wp::application::resourcesystem::ResourceException(resource, "TrackMeshes contains an empty <Mesh> name.");
      if (!seen.insert(name).second)
        throw wp::application::resourcesystem::ResourceException(resource, "TrackMeshes contains duplicate mesh '" + name + "'.");
      mapRes->mTrackMeshNames.push_back(std::move(name));
    } while (meshNode->next());
  }
  if (mapRes->mTrackMeshNames.empty())
    throw wp::application::resourcesystem::ResourceException(
        resource, "Track definition requires at least one <TrackMeshes><Mesh> entry -- this track resource "
                  "predates the collision-mesh export contract and must be re-exported from the editor.");
}
