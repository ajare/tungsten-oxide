#include <optional>
#include <set>

#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include "MapTungstenMonoxideDefinitionFactory.h"
#include "Map.h"

MapTungstenMonoxideDefinitionFactory::MapTungstenMonoxideDefinitionFactory()
    : applib::MapResourceDefinitionFactory("Track") {
}

namespace {

mono::ModelMeshType parseMeshType(std::string const& value) {
  if (value == "Track") return mono::ModelMeshType::Track;
  if (value == "Decorative") return mono::ModelMeshType::Decorative;
  return mono::ModelMeshType::Physical;
}

}  // namespace

// Parses the <Models> list (TRACK_MODEL_LIST_PLAN.md Milestone 7) via wp::XmlNode -- an independent
// reimplementation of the same documented <Model> fragment shape cpp/model-xml parses via TinyXML2
// for the editor/model-tool, per that plan's own architecture note on why this host doesn't link
// cpp/model-xml. Exactly one <Model> must carry a Type=Track mesh (and therefore <TrackData>) --
// that one becomes the primary, resolved into mModelFileName/mTrackDataFileName exactly as the old
// bare <ModelFile>/<TrackData> pair used to be. The old flat <TrackMeshes> list is gone entirely --
// see Map.h's own comment on why deriving it from the baked Track loses nothing.
void MapTungstenMonoxideDefinitionFactory::create(
    wp::application::resourcesystem::Resource* resource,
    wp::application::resourcesystem::ResourceManager* resourceMgr,
    wp::XmlNode* node) {
  VAR_UNUSED(resourceMgr);
  auto mapRes = static_cast<Map*>(resource);

  auto modelsNode = node->getChild("Models");
  auto modelNode = modelsNode->getOptionalChild("Model");
  std::optional<std::size_t> primaryIndex;
  std::set<std::string> seenIds;
  if (modelNode) {
    do {
      mono::EmbeddedModelRef model;
      if (!modelNode->getOptionalAttribute("id", model.id) || model.id.empty())
        throw wp::application::resourcesystem::ResourceException(resource, "<Model> is missing a non-empty id attribute.");
      if (!seenIds.insert(model.id).second)
        throw wp::application::resourcesystem::ResourceException(resource, "<Models> contains duplicate Model id '" + model.id + "'.");

      model.modelFileReference = modelNode->getChild("ModelFile")->getValue();
      if (model.modelFileReference.empty())
        throw wp::application::resourcesystem::ResourceException(resource, "<Model id=\"" + model.id + "\"> is missing a non-empty <ModelFile>.");

      auto trackDataNode = modelNode->getOptionalChild("TrackData");
      std::string const trackDataReference = trackDataNode != nullptr ? trackDataNode->getValue() : "";

      auto meshesNode = modelNode->getOptionalChild("Meshes");
      auto meshNode = meshesNode != nullptr ? meshesNode->getOptionalChild("Mesh") : nullptr;
      bool hasTrackMesh = false;
      if (meshNode) {
        do {
          mono::ModelMeshMeta mesh;
          mesh.name = meshNode->getChild("Name")->getValue();
          mesh.type = parseMeshType(meshNode->getChild("Type")->getValue());
          auto visibleNode = meshNode->getOptionalChild("Visible");
          std::string const visibleText = visibleNode != nullptr ? visibleNode->getValue() : "true";
          mesh.visible = visibleText != "false" && visibleText != "0";
          if (mesh.type == mono::ModelMeshType::Track) hasTrackMesh = true;
          model.meshes.push_back(std::move(mesh));
        } while (meshNode->next());
      }

      if (hasTrackMesh) {
        if (trackDataReference.empty())
          throw wp::application::resourcesystem::ResourceException(
              resource, "<Model id=\"" + model.id + "\"> has a Type=Track mesh but no <TrackData>.");
        if (primaryIndex.has_value())
          throw wp::application::resourcesystem::ResourceException(
              resource, "<Models> contains more than one Type=Track Model -- only one is currently supported.");
        primaryIndex = mapRes->mEmbeddedModels.size();
        mapRes->mModelFileName = model.modelFileReference;
        mapRes->mTrackDataFileName = trackDataReference;
      }

      mapRes->mEmbeddedModels.push_back(std::move(model));
    } while (modelNode->next());
  }

  if (!primaryIndex.has_value())
    throw wp::application::resourcesystem::ResourceException(
        resource, "Track definition requires exactly one Model with a Type=Track mesh and <TrackData> in its <Models> list.");
}
