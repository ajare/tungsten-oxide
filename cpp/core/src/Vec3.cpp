// Vec3.cpp — the double-precision Vec3 bodies (declared in include/Vec3.hpp).
//
// Do NOT "optimize" these method bodies — parity with the JS oracle depends on
// the exact operation order (float add isn't associative):
//   * normalize() divides by length()||1, so a zero vector stays zero (not NaN).
//   * applyQuaternion() uses r128's inverse-quaternion form (ix/iy/iz/iw), NOT
//     the t = 2*cross form three.js adopted around r150 — they differ in the last
//     ULP.
//   * applyAxisAngle() goes through setFromAxisAngle -> applyQuaternion, as THREE
//     does; it is not a direct Rodrigues rotation.
#include "Vec3.hpp"
#include <cmath>

namespace tox {

Vec3::Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

Vec3& Vec3::set(double x_, double y_, double z_) {
  x = x_;
  y = y_;
  z = z_;
  return *this;
}
Vec3 Vec3::clone() const { return Vec3(x, y, z); }
Vec3& Vec3::copy(const Vec3& v) {
  x = v.x;
  y = v.y;
  z = v.z;
  return *this;
}

Vec3& Vec3::add(const Vec3& v) {
  x += v.x;
  y += v.y;
  z += v.z;
  return *this;
}
Vec3& Vec3::addVectors(const Vec3& a, const Vec3& b) {
  x = a.x + b.x;
  y = a.y + b.y;
  z = a.z + b.z;
  return *this;
}
Vec3& Vec3::addScaledVector(const Vec3& v, double s) {
  x += v.x * s;
  y += v.y * s;
  z += v.z * s;
  return *this;
}
Vec3& Vec3::sub(const Vec3& v) {
  x -= v.x;
  y -= v.y;
  z -= v.z;
  return *this;
}
Vec3& Vec3::subVectors(const Vec3& a, const Vec3& b) {
  x = a.x - b.x;
  y = a.y - b.y;
  z = a.z - b.z;
  return *this;
}
Vec3& Vec3::multiplyScalar(double s) {
  x *= s;
  y *= s;
  z *= s;
  return *this;
}
Vec3& Vec3::divideScalar(double s) { return multiplyScalar(1.0 / s); }

// Rotate about a (unit) axis by angle radians, via a quaternion — THREE's path.
Vec3& Vec3::applyAxisAngle(const Vec3& axis, double angle) {
  const double halfAngle = angle / 2.0, s = std::sin(halfAngle);
  return applyQuaternion(axis.x * s, axis.y * s, axis.z * s, std::cos(halfAngle));
}

// Vector3.applyQuaternion (r128), kept in this exact operation order.
Vec3& Vec3::applyQuaternion(double qx, double qy, double qz, double qw) {
  const double vx = x, vy = y, vz = z;
  // calculate quat * vector
  const double ix = qw * vx + qy * vz - qz * vy;
  const double iy = qw * vy + qz * vx - qx * vz;
  const double iz = qw * vz + qx * vy - qy * vx;
  const double iw = -qx * vx - qy * vy - qz * vz;
  // calculate result * inverse quat
  x = ix * qw + iw * -qx + iy * -qz - iz * -qy;
  y = iy * qw + iw * -qy + iz * -qx - ix * -qz;
  z = iz * qw + iw * -qz + ix * -qy - iy * -qx;
  return *this;
}

double Vec3::dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
Vec3& Vec3::crossVectors(const Vec3& a, const Vec3& b) {
  const double ax = a.x, ay = a.y, az = a.z;
  const double bx = b.x, by = b.y, bz = b.z;
  x = ay * bz - az * by;
  y = az * bx - ax * bz;
  z = ax * by - ay * bx;
  return *this;
}

double Vec3::lengthSq() const { return x * x + y * y + z * z; }
double Vec3::length() const { return std::sqrt(x * x + y * y + z * z); }

// Zero-length stays zero (divide by 1), never NaN.
Vec3& Vec3::normalize() {
  double l = length();
  return divideScalar(l != 0.0 ? l : 1.0);
}

Vec3& Vec3::lerp(const Vec3& v, double alpha) {
  x += (v.x - x) * alpha;
  y += (v.y - y) * alpha;
  z += (v.z - z) * alpha;
  return *this;
}

double Vec3::distanceToSquared(const Vec3& v) const {
  const double dx = x - v.x, dy = y - v.y, dz = z - v.z;
  return dx * dx + dy * dy + dz * dz;
}
double Vec3::distanceTo(const Vec3& v) const { return std::sqrt(distanceToSquared(v)); }

Vec3& Vec3::negate() {
  x = -x;
  y = -y;
  z = -z;
  return *this;
}

}  // namespace tox
