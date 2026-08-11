#include "willpower/application/resourcesystem/ImageResourceDefinitionFactory.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;

ImageResourceDefinitionFactory::ImageResourceDefinitionFactory(string const& factoryType)
    : ResourceDefinitionFactory("Image", factoryType) {
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE