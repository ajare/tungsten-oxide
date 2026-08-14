#include "willpower/common/StringUtils.h"
#include "willpower/common/DataNode.h"
#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ImageDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;

ImageDefaultDefinitionFactory::ImageDefaultDefinitionFactory()
    : ImageResourceDefinitionFactory("") {
}

void ImageDefaultDefinitionFactory::create(Resource* resource, ResourceManager* resourceMgr, DataNode* node) {
  WP_UNUSED(resource);
  WP_UNUSED(resourceMgr);
  WP_UNUSED(node);
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE