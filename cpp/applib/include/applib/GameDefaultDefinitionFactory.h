#pragma once

#include "Platform.h"
#include "GameResourceDefinitionFactory.h"

namespace applib {
class APPLIB_API GameDefaultDefinitionFactory : public GameResourceDefinitionFactory {
public:
  GameDefaultDefinitionFactory();

  void create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::DataNode* node) override;
};

}  // namespace applib
