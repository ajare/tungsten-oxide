#pragma once

#include "Platform.h"
#include "ProtoEntityResourceDefinitionFactory.h"

namespace applib {
class APPLIB_API ProtoEntityDefaultDefinitionFactory : public ProtoEntityResourceDefinitionFactory {
  void createProtoEntity(ProtoEntity* entity, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::DataNode* node) override;

public:
  ProtoEntityDefaultDefinitionFactory();
};

}  // namespace applib
