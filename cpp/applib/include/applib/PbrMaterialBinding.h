#pragma once

#include <map>
#include <string>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

#include "Platform.h"

namespace applib {
class PbrMaterialBindingDefaultDefinitionFactory;

class APPLIB_API PbrMaterialBinding : public wp::application::resourcesystem::Resource {
  friend class PbrMaterialBindingDefaultDefinitionFactory;

  std::string mBinding;

public:
  PbrMaterialBinding(std::string const& name,
                     std::string const& namesp,
                     std::string const& source,
                     std::map<std::string, std::string> const& tags,
                     wp::application::resourcesystem::ResourceLocation* location);

  std::string const& getBinding() const;
};

class APPLIB_API PbrMaterialBindingResourceFactory : public wp::application::resourcesystem::ResourceFactory {
public:
  PbrMaterialBindingResourceFactory()
      : wp::application::resourcesystem::ResourceFactory("PbrMaterialBinding") {
  }

  wp::application::resourcesystem::Resource* createResource(
      std::string const& name,
      std::string const& namesp,
      std::string const& source,
      std::map<std::string, std::string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location) override {
    return new PbrMaterialBinding(name, namesp, source, tags, location);
  }
};
}  // namespace applib
