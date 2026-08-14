#pragma once

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/AnimationSetResourceDefinitionFactory.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
class WP_APPLICATION_API AnimationSetDefaultDefinitionFactory : public AnimationSetResourceDefinitionFactory {
public:
  AnimationSetDefaultDefinitionFactory();

  void create(Resource* resource, ResourceManager* resourceMgr, wp::DataNode* node) override;
};

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
