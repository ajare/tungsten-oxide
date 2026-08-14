#pragma once

#include "PbrMaterialBindingResourceDefinitionFactory.h"

namespace applib {
class APPLIB_API PbrMaterialBindingDefaultDefinitionFactory : public PbrMaterialBindingResourceDefinitionFactory {
public:
  PbrMaterialBindingDefaultDefinitionFactory();

  void create(wp::application::resourcesystem::Resource* resource,
              wp::application::resourcesystem::ResourceManager* resourceMgr,
              wp::DataNode* node) override;
};
}  // namespace applib
