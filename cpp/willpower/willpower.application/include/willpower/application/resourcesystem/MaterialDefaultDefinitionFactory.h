#pragma once

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/MaterialResourceDefinitionFactory.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
class WP_APPLICATION_API MaterialDefaultDefinitionFactory : public MaterialResourceDefinitionFactory {
public:
  MaterialDefaultDefinitionFactory();

  void create(Resource* resource, ResourceManager* resourceMgr, wp::DataNode* node) override;
};

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
