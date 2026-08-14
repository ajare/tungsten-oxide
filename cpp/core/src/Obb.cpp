#include "Obb.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tox {
namespace {

// A candidate separating axis shorter than this (squared) is degenerate -- it only arises when the
// two vectors crossed to build it are parallel, and that case is already covered by one of the face
// normals, so skipping it never loses a real separation.
constexpr double AXIS_LENGTH_SQ_EPSILON = 1e-16;

}  // namespace

Vec3 Obb::corner(int index) const {
  Vec3 result = center;
  for (int axis = 0; axis < 3; ++axis)
    result += axes[axis] * ((index & (1 << axis)) ? halfExtents[axis] : -halfExtents[axis]);
  return result;
}

bool overlapsTriangle(const Obb& obb, const CollisionTriangle& triangle, Vec3* outNormal,
                      double* outDepth) {
  // Everything below works in the box's own centered frame: the triangle moves, the box stays at
  // the origin with its interval along any axis `a` being [-radius(a), +radius(a)].
  const Vec3 vertices[3] = {triangle.positions[0] - obb.center, triangle.positions[1] - obb.center,
                            triangle.positions[2] - obb.center};
  const Vec3 edges[3] = {vertices[1] - vertices[0], vertices[2] - vertices[1],
                         vertices[0] - vertices[2]};

  double bestDepth = std::numeric_limits<double>::infinity();
  Vec3 bestNormal(0.0, 1.0, 0.0);

  // Returns false as soon as `rawAxis` separates the two, otherwise records its overlap depth as a
  // minimum-translation-vector candidate.
  const auto overlapsAlong = [&](const Vec3& rawAxis) {
    const double lengthSq = glm::dot(rawAxis, rawAxis);
    if (lengthSq < AXIS_LENGTH_SQ_EPSILON) return true;
    const Vec3 axis = rawAxis / std::sqrt(lengthSq);

    double lo = glm::dot(vertices[0], axis), hi = lo;
    for (int i = 1; i < 3; ++i) {
      const double projected = glm::dot(vertices[i], axis);
      lo = std::min(lo, projected);
      hi = std::max(hi, projected);
    }
    const double radius = std::fabs(glm::dot(obb.axes[0], axis)) * obb.halfExtents[0] +
                          std::fabs(glm::dot(obb.axes[1], axis)) * obb.halfExtents[1] +
                          std::fabs(glm::dot(obb.axes[2], axis)) * obb.halfExtents[2];
    if (lo > radius || hi < -radius) return false;

    // Two ways out along this axis: shove the box to +axis until its near face clears the
    // triangle's far end, or to -axis until its far face clears the triangle's near end. The
    // shorter of the two is this axis's contribution to the MTV.
    const double pushPositive = hi + radius;
    const double pushNegative = radius - lo;
    const double depth = std::min(pushPositive, pushNegative);
    if (depth < bestDepth) {
      bestDepth = depth;
      bestNormal = pushPositive <= pushNegative ? axis : -axis;
    }
    return true;
  };

  // 3 box face normals + the triangle's own normal.
  for (const Vec3& axis : obb.axes)
    if (!overlapsAlong(axis)) return false;
  if (!overlapsAlong(glm::cross(edges[0], edges[1]))) return false;
  // 9 edge-vs-edge axes.
  for (const Vec3& boxAxis : obb.axes)
    for (const Vec3& edge : edges)
      if (!overlapsAlong(glm::cross(boxAxis, edge))) return false;

  if (outNormal) *outNormal = bestNormal;
  if (outDepth) *outDepth = bestDepth;
  return true;
}

bool overlapsAabb(const Obb& obb, const TrackCollisionSurface::Bounds& bounds) {
  const Vec3 boxCenter = (bounds.lo + bounds.hi) * 0.5;
  const Vec3 boxHalf = (bounds.hi - bounds.lo) * 0.5;
  const Vec3 delta = obb.center - boxCenter;

  // World axes: the AABB projects to its own half extent, the OBB to the sum of its half extents
  // weighted by how much each of its axes leans along this world axis.
  for (int axis = 0; axis < 3; ++axis) {
    const double radius = boxHalf[axis] + std::fabs(obb.axes[0][axis]) * obb.halfExtents[0] +
                          std::fabs(obb.axes[1][axis]) * obb.halfExtents[1] +
                          std::fabs(obb.axes[2][axis]) * obb.halfExtents[2];
    if (std::fabs(delta[axis]) > radius) return false;
  }
  // Box axes: mirror image of the above.
  for (int axis = 0; axis < 3; ++axis) {
    const Vec3& a = obb.axes[axis];
    const double radius = obb.halfExtents[axis] + std::fabs(a.x) * boxHalf.x +
                          std::fabs(a.y) * boxHalf.y + std::fabs(a.z) * boxHalf.z;
    if (std::fabs(glm::dot(delta, a)) > radius) return false;
  }
  return true;
}

}  // namespace tox
