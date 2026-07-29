#include "willpower/application/resourcesystem/DirectoryResourceLocation.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

#include <utils/FileSystem.h>

#include "Game.h"

using namespace std;
using namespace wp;

Game::Game(string const& name,
           string const& namesp,
           string const& source,
           map<string, string> const& tags,
           application::resourcesystem::ResourceLocation* location)
    : applib::Game(name, namesp, source, tags, location) {
}

string Game::getShipModelPath() const {
  auto location = dynamic_cast<application::resourcesystem::DirectoryResourceLocation*>(mwLocation);
  if (!location)
    throw application::resourcesystem::ResourceException(const_cast<Game*>(this), "Game ship model requires a directory resource location.");
  return utils::FileSystem::concatPaths(location->getRootPath(), mShipModelFile);
}
