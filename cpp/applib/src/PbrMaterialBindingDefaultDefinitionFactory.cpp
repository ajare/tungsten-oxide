#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include "applib/PbrMaterialBinding.h"
#include "applib/PbrMaterialBindingDefaultDefinitionFactory.h"

namespace applib {
PbrMaterialBindingDefaultDefinitionFactory::PbrMaterialBindingDefaultDefinitionFactory()
    : PbrMaterialBindingResourceDefinitionFactory("") {
}

void PbrMaterialBindingDefaultDefinitionFactory::create(
    wp::application::resourcesystem::Resource* resource,
    wp::application::resourcesystem::ResourceManager* resourceMgr,
    wp::XmlNode* node) {
  (void)resourceMgr;
  auto binding = static_cast<PbrMaterialBinding*>(resource);
  binding->mBinding = node->getChild("Binding")->getValue();
  if (binding->mBinding.find_first_not_of(" \t\r\n") == std::string::npos) {
    throw wp::application::resourcesystem::ResourceException(resource, "PbrMaterialBinding/Binding must not be empty.");
  }
}
}  // namespace applib
