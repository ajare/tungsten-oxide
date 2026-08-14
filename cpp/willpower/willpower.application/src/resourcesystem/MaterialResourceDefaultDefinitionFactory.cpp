#include "willpower/common/DataNode.h"
#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/MaterialDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;

MaterialDefaultDefinitionFactory::MaterialDefaultDefinitionFactory()
    : MaterialResourceDefinitionFactory("") {
}

void MaterialDefaultDefinitionFactory::create(Resource* resource, ResourceManager* resourceMgr, DataNode* node) {
  WP_UNUSED(resourceMgr);

  auto materialRes = static_cast<MaterialResource*>(resource);

  auto texturesNode = node->getChild("Textures");
  auto textureNode = texturesNode->getOptionalChild("Texture");
  if (textureNode) {
    do {
      string textureType = textureNode->getProperty("type");

      if (textureType == "resource") {
        addResourceTextureDefinition(materialRes, textureNode->getProperty("sampler"), textureNode->getValue());
      } else if (textureType == "default") {
        addDefaultTextureDefinition(materialRes, textureNode->getProperty("sampler"));
      } else {
        string errMsg = "material '" + materialRes->getName() +
                        "' has invalid texture type '" + textureType + "'.";

        throw ResourceException(materialRes, errMsg);
      }
    } while (textureNode->next());
  }
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE