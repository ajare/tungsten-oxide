// Vec3.hpp — tox::Vec3 is glm::dvec3 (double precision). This engine previously used a hand-rolled
// Vec3 with a specific operation order pinned to the golden fixture corpus (cpp/test-data/); it has
// been migrated to glm, so physics now uses glm's normalize/cross/quaternion-rotation implementations
// directly. Fixture tolerances were loosened accordingly — see cpp/test-data/traces/README.md.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace tox {

using Vec3 = glm::dvec3;

// glm::normalize() on a zero-length vector produces NaN. The physics code relies on a zero vector
// staying zero instead (ships/tangents that happen to be exactly zero-length must not go NaN).
inline Vec3 normalizeSafe(const Vec3& v) {
  const double l = glm::length(v);
  return l != 0.0 ? v / l : v;
}

// Rotate v about (unit) axis by angle radians.
inline Vec3 applyAxisAngle(const Vec3& v, const Vec3& axis, double angle) {
  return glm::angleAxis(angle, axis) * v;
}

}  // namespace tox
