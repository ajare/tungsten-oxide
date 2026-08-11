#include "applib/PbrMaterialBindingResourceDefinitionFactory.h"

namespace applib {
PbrMaterialBindingResourceDefinitionFactory::PbrMaterialBindingResourceDefinitionFactory(std::string const& factoryType)
    : ResourceDefinitionFactory("PbrMaterialBinding", factoryType) {
}
}  // namespace applib
