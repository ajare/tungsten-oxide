#pragma once

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/XmlFileResourceDefinitionFactory.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
class WP_APPLICATION_API XmlFileDefaultDefinitionFactory : public XmlFileResourceDefinitionFactory {
public:
  XmlFileDefaultDefinitionFactory();

  void create(Resource* resource, ResourceManager* resourceMgr, DataNode* node) override;
};

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
