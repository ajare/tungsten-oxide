#include "applib/PbrMaterialBinding.h"

namespace applib {
PbrMaterialBinding::PbrMaterialBinding(
    std::string const& name,
    std::string const& namesp,
    std::string const& source,
    std::map<std::string, std::string> const& tags,
    wp::application::resourcesystem::ResourceLocation* location)
    : Resource(name, namesp, "PbrMaterialBinding", source, tags, location) {
}

std::string const& PbrMaterialBinding::getBinding() const {
  return mBinding;
}
}  // namespace applib
