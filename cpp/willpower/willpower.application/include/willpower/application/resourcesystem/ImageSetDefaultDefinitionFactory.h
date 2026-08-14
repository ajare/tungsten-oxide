#pragma once

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/ImageSetResourceDefinitionFactory.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
class WP_APPLICATION_API ImageSetDefaultDefinitionFactory : public ImageSetResourceDefinitionFactory {
public:
  ImageSetDefaultDefinitionFactory();

  void create(Resource* resource, ResourceManager* resourceMgr, wp::DataNode* node) override;
};

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
