// Track.cpp — bodies for the baked-corridor structs declared in
// include/Track.hpp (the structs themselves are plain data).
#include "Track.hpp"

namespace tox {

bool Track::endpointConnected(const std::string& id, bool present) const {
  return present && connectedEndpointIds.count(id) != 0;
}

}  // namespace tox
