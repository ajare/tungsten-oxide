#include "CollidableFlag.hpp"

namespace modeltool {

std::string encodeCollidableInName(const std::string& name, bool collidable) {
  if (collidable) return name;
  return name + kDecorativeMeshNameSuffix;
}

DecodedMeshName decodeCollidableFromName(const std::string& name) {
  constexpr std::size_t suffixLen = sizeof(kDecorativeMeshNameSuffix) - 1;
  if (name.size() >= suffixLen && name.compare(name.size() - suffixLen, suffixLen, kDecorativeMeshNameSuffix) == 0)
    return {name.substr(0, name.size() - suffixLen), false};
  return {name, true};
}

}  // namespace modeltool
