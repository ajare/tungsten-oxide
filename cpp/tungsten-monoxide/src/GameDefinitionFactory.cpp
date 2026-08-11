#include <filesystem>

#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include "Game.h"
#include "GameDefinitionFactory.h"

GameDefinitionFactory::GameDefinitionFactory()
    : applib::GameDefaultDefinitionFactory() {
}

void GameDefinitionFactory::create(
    wp::application::resourcesystem::Resource* resource,
    wp::application::resourcesystem::ResourceManager* resourceMgr,
    wp::XmlNode* node) {
  VAR_UNUSED(resourceMgr);
  auto game = static_cast<Game*>(resource);
  auto ship = node->getChild("ShipModel");
  game->mShipModelFile = ship->getChild("ModelFile")->getValue();
  game->mShipMaterialBinding = ship->getChild("Material")->getValue();
  std::filesystem::path modelPath(game->mShipModelFile);
  if (modelPath.empty() || modelPath.is_absolute() || modelPath.has_root_name() || modelPath.has_root_directory())
    throw wp::application::resourcesystem::ResourceException(resource, "Game ShipModel/ModelFile must be relative.");
  for (auto const& part : modelPath)
    if (part == "..")
      throw wp::application::resourcesystem::ResourceException(resource, "Game ShipModel/ModelFile may not traverse outside the resource directory.");
  if (game->mShipMaterialBinding.empty())
    throw wp::application::resourcesystem::ResourceException(resource, "Game ShipModel/Material must not be empty.");
}
