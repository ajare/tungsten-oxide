#include <utils/StringUtils.h>

#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/common/Exceptions.h"

#include "Game.h"
#include "GameDefaultDefinitionFactory.h"

namespace applib {
using namespace std;
using namespace utils;
using namespace wp;

GameDefaultDefinitionFactory::GameDefaultDefinitionFactory()
    : GameResourceDefinitionFactory("") {
}

void GameDefaultDefinitionFactory::create(application::resourcesystem::Resource* resource, application::resourcesystem::ResourceManager* resourceMgr, DataNode* node) {
}

}  // namespace applib
