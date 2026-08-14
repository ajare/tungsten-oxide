#include "willpower/application/resourcesystem/ResourceExceptions.h"

#include "applib/EntityProperties.h"

#include "ProtoEntity.h"
#include "EntityType.h"
#include "EntityStats.h"

using namespace std;
using namespace wp;

map<string, EntityType> gsReferenceMapping = {
    {"Player", EntityType::Player}};

ProtoEntity::ProtoEntity(string const& name,
                         string const& namesp,
                         string const& source,
                         map<string, string> const& tags,
                         application::resourcesystem::ResourceLocation* location,
                         shared_ptr<applib::EntityHandler> entityHandler)
    : applib::ProtoEntity(name, namesp, source, tags, location, entityHandler) {
}

void ProtoEntity::loadExtraDefinitions(wp::DataNode* node, entt::entity protoId) {
  auto statsNode = node->getOptionalChild("Stats");
  if (statsNode) {
    mEntityHandler->registerProtoComponent<EntityStats>(protoId, {});
  }
}