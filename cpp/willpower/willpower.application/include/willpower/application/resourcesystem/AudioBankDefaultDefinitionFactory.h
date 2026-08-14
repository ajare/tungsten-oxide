#pragma once

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/AudioBankResourceDefinitionFactory.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {

class WP_APPLICATION_API AudioBankDefaultDefinitionFactory : public AudioBankResourceDefinitionFactory {
public:
  AudioBankDefaultDefinitionFactory();

  void create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::DataNode* node) override;
};

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
