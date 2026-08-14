#include "willpower/common/StringUtils.h"

#include "willpower/application/resourcesystem/AudioBankDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/AudioBankResource.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/common/Exceptions.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;
using namespace wp;

AudioBankDefaultDefinitionFactory::AudioBankDefaultDefinitionFactory()
    : AudioBankResourceDefinitionFactory("") {
}

void AudioBankDefaultDefinitionFactory::create(application::resourcesystem::Resource* resource, application::resourcesystem::ResourceManager* resourceMgr, wp::DataNode* node) {
  auto bankRes = static_cast<AudioBankResource*>(resource);
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
