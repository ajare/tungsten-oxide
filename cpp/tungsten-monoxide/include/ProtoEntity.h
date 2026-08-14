#pragma once

#include <string>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

#include <applib/Entity.h>
#include <applib/ProtoEntity.h>
#include <applib/EntityHandler.h>

#include "Platform.h"

class ProtoEntity : public applib::ProtoEntity {
  void loadExtraDefinitions(wp::DataNode* node, entt::entity protoId) override;

public:
  ProtoEntity(std::string const& name,
              std::string const& namesp,
              std::string const& source,
              std::map<std::string, std::string> const& tags,
              wp::application::resourcesystem::ResourceLocation* location,
              std::shared_ptr<applib::EntityHandler> entityHandler);
};

class ProtoEntityResourceFactory : public wp::application::resourcesystem::ResourceFactory {
  std::shared_ptr<applib::EntityHandler> mEntityHandler;

public:
  ProtoEntityResourceFactory(std::shared_ptr<applib::EntityHandler> entityHandler)
      : wp::application::resourcesystem::ResourceFactory("ProtoEntity"), mEntityHandler(entityHandler) {
  }

  wp::application::resourcesystem::Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, wp::application::resourcesystem::ResourceLocation* location) override {
    return new ProtoEntity(name, namesp, source, tags, location, mEntityHandler);
  }
};