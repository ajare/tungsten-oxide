#pragma once

#include "Platform.h"
#include "MapResourceDefinitionFactory.h"

namespace applib {
class APPLIB_API MapDefaultDefinitionFactory : public MapResourceDefinitionFactory {
public:
  MapDefaultDefinitionFactory();

  void create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::DataNode* node) override;
};

}  // namespace applib
