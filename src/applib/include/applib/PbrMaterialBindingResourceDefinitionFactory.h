#pragma once

#include <string>

#include <willpower/application/resourcesystem/ResourceDefinitionFactory.h>

#include "Platform.h"

namespace applib {
class APPLIB_API PbrMaterialBindingResourceDefinitionFactory : public wp::application::resourcesystem::ResourceDefinitionFactory {
public:
  explicit PbrMaterialBindingResourceDefinitionFactory(std::string const& factoryType);
};
}  // namespace applib
