// Vec3.hpp — hand-rolled double-precision 3D vector. No glm, no Eigen: this engine's physics was
// independently verified step-for-step against a historical reference implementation, and several
// operations (op order, zero-length `normalize()`, a specific inverse-quaternion `applyQuaternion`
// convention) must keep that exact behavior rather than the more "obvious" formula, or the
// committed golden fixture corpus (cpp/test-data/) stops matching to its pinned tolerance.
//
// Declarations only; the parity-critical method bodies (and the "do NOT optimize"
// notes about op order / zero-length / the quaternion path) live in src/Vec3.cpp.
#pragma once

namespace tox {

struct Vec3 {
  double x{0.0}, y{0.0}, z{0.0};

  Vec3() = default;
  Vec3(double x_, double y_, double z_);

  Vec3& set(double x_, double y_, double z_);
  Vec3 clone() const;
  Vec3& copy(const Vec3& v);

  Vec3& add(const Vec3& v);
  Vec3& addVectors(const Vec3& a, const Vec3& b);
  Vec3& addScaledVector(const Vec3& v, double s);
  Vec3& sub(const Vec3& v);
  Vec3& subVectors(const Vec3& a, const Vec3& b);
  Vec3& multiplyScalar(double s);
  Vec3& divideScalar(double s);

  Vec3& applyAxisAngle(const Vec3& axis, double angle);
  Vec3& applyQuaternion(double qx, double qy, double qz, double qw);

  double dot(const Vec3& v) const;
  Vec3& crossVectors(const Vec3& a, const Vec3& b);

  double lengthSq() const;
  double length() const;
  Vec3& normalize();

  Vec3& lerp(const Vec3& v, double alpha);

  double distanceToSquared(const Vec3& v) const;
  double distanceTo(const Vec3& v) const;

  Vec3& negate();
};

}  // namespace tox
