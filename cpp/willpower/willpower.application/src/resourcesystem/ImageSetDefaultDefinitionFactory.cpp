#include "willpower/common/DataNode.h"
#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ImageSetDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;

ImageSetDefaultDefinitionFactory::ImageSetDefaultDefinitionFactory()
    : ImageSetResourceDefinitionFactory("") {
}

void ImageSetDefaultDefinitionFactory::create(Resource* resource, ResourceManager* resourceMgr, DataNode* node) {
  WP_UNUSED(resourceMgr);

  auto imageSetRes = static_cast<ImageSetResource*>(resource);

  clear(imageSetRes);

  auto imagesNode = node->getChild("Images");
  auto imageNode = imagesNode->getOptionalChild("Image");
  if (imageNode) {
    do {
      string imageName = imageNode->getProperty("name");
      if (imageSetRes->hasImageDefinition(imageName)) {
        throw ResourceException(imageSetRes, "image with the name '" + imageName + "' specified multiple times.");
      }

      addImageDefinition(imageSetRes, imageName, createImageDefinition(imageSetRes, imageName, imageNode));
    } while (imageNode->next());
  }

  // Image sets
  imageNode = imagesNode->getOptionalChild("ImageSet");
  if (imageNode) {
    do {
      string imageSetName = imageNode->getProperty("name");
      if (imageSetRes->hasImageDefinitionSet(imageSetName)) {
        throw ResourceException(imageSetRes, "imageset with the name '" + imageSetName + "' specified multiple times.");
      }

      auto baseDef = createImageDefinition(imageSetRes, imageSetName, imageNode);

      // Calculate set details
      uint32_t count = StringUtils::parseUInt(imageNode->getProperty("count"));
      int32_t dx = StringUtils::parseInt(imageNode->getProperty("dx"));
      int32_t dy = StringUtils::parseInt(imageNode->getProperty("dy"));

      vector<ImageSetResource::ImageDefinition> imageDefs;

      // Need to work with ints in case dx or dy are negative
      int baseX = (int)baseDef.x;
      int baseY = (int)baseDef.y;
      for (uint32_t i = 0; i < count; ++i) {
        validateCoordinates(
            imageSetRes,
            imageSetName,
            baseX,
            baseY,
            (int)baseDef.width,
            (int)baseDef.height);

        baseDef.x = (uint32_t)baseX;
        baseDef.y = (uint32_t)baseY;

        calculateUvCoords(imageSetRes, &baseDef);

        imageDefs.push_back(baseDef);

        baseX += dx;
        baseY += dy;
      }

      addImageDefinitionSet(imageSetRes, imageSetName, imageDefs);
    } while (imageNode->next());
  }
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE