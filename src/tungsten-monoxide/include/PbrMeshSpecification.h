#pragma once

#include <mpp/mesh/MeshSpecification.h>

namespace mono {
// Shared game-model contract. PBR streams use the package-authored 52-byte
// position/normal/uv/colour/tangent layout; compatibility streams omit tangent4.
mpp::mesh::MeshSpecification gameMeshSpecification(bool indexed, bool pbr);
}  // namespace mono
