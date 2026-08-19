#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include <willpower/application/resourcesystem/ResourceExceptions.h>
#include <willpower/common/DataNode.h>

#include <applib/PbrMaterialBinding.h>
#include <applib/PbrMaterialBindingDefaultDefinitionFactory.h>

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void parseBinding(applib::PbrMaterialBinding& binding, std::string const& value) {
  StructuredData definition("Definition");
  definition.addEntry("Binding", value);
  wp::DataNode node(definition);
  applib::PbrMaterialBindingDefaultDefinitionFactory factory;
  factory.create(&binding, nullptr, &node);
}
}  // namespace

int main() {
  try {
    applib::PbrMaterialBindingResourceFactory resourceFactory;
    std::unique_ptr<wp::application::resourcesystem::Resource> created(
        resourceFactory.createResource("AsphaltPbr", "Tracks", "", {}, nullptr));
    require(created->getType() == "PbrMaterialBinding", "resource factory returned the wrong type");
    auto binding = dynamic_cast<applib::PbrMaterialBinding*>(created.get());
    require(binding != nullptr, "resource factory returned the wrong class");

    parseBinding(*binding, "Track.Asphalt");
    require(binding->getBinding() == "Track.Asphalt", "definition did not retain the logical binding");
    require(!binding->getMppResource(), "binding resources must not create an MPP resource");

    applib::PbrMaterialBinding empty("Empty", "", "", {}, nullptr);
    bool rejected = false;
    try {
      parseBinding(empty, "");
    } catch (wp::application::resourcesystem::ResourceException const&) {
      rejected = true;
    }
    require(rejected, "empty logical binding was accepted");

    std::cout << "PbrMaterialBinding tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
