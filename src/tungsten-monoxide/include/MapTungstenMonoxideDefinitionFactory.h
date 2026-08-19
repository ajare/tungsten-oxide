#pragma once

#include <applib/MapResourceDefinitionFactory.h>

#include "Platform.h"

class MapTungstenMonoxideDefinitionFactory : public applib::MapResourceDefinitionFactory {
public:
  MapTungstenMonoxideDefinitionFactory();

  void create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::DataNode* node) override;
};
