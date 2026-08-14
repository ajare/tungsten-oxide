#pragma once

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/ShaderResourceDefinitionFactory.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
class WP_APPLICATION_API ShaderDefaultDefinitionFactory : public ShaderResourceDefinitionFactory {
public:
  ShaderDefaultDefinitionFactory();

  void create(Resource* resource, ResourceManager* resourceMgr, wp::DataNode* node) override;
};

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
