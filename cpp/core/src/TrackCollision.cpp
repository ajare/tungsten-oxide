#include "TrackCollision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace tox {
namespace {
constexpr double EPSILON = 1e-9;

Vec3 minVec(const Vec3& a, const Vec3& b) {
  return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
Vec3 maxVec(const Vec3& a, const Vec3& b) {
  return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

double component(const Vec3& v, int axis) {
  return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

bool segmentBounds(const Vec3& from, const Vec3& to, const TrackCollisionSurface::Bounds& bounds) {
  const Vec3 d = to - from;
  double lo = 0.0, hi = 1.0;
  for (int axis = 0; axis < 3; ++axis) {
    const double p = component(from, axis), q = component(d, axis);
    const double b0 = component(bounds.lo, axis) - EPSILON;
    const double b1 = component(bounds.hi, axis) + EPSILON;
    if (std::fabs(q) < EPSILON) {
      if (p < b0 || p > b1) return false;
      continue;
    }
    double t0 = (b0 - p) / q, t1 = (b1 - p) / q;
    if (t0 > t1) std::swap(t0, t1);
    lo = std::max(lo, t0);
    hi = std::min(hi, t1);
    if (lo > hi) return false;
  }
  return true;
}

std::optional<CollisionHit> intersect(const CollisionTriangle& triangle, int triangleIndex,
                                      const Vec3& from, const Vec3& to, bool swept) {
  const Vec3 d = to - from;
  const Vec3 e1 = triangle.positions[1] - triangle.positions[0];
  const Vec3 e2 = triangle.positions[2] - triangle.positions[0];
  Vec3 p = glm::cross(d, e2);
  const double det = glm::dot(e1, p);
  if (std::fabs(det) < EPSILON) return std::nullopt;
  const double invDet = 1.0 / det;
  const Vec3 s = from - triangle.positions[0];
  const double u = glm::dot(s, p) * invDet;
  if (u < -EPSILON || u > 1.0 + EPSILON) return std::nullopt;
  Vec3 q = glm::cross(s, e1);
  const double v = glm::dot(d, q) * invDet;
  if (v < -EPSILON || u + v > 1.0 + EPSILON) return std::nullopt;
  const double t = glm::dot(e2, q) * invDet;
  if (t < -EPSILON || t > 1.0 + EPSILON) return std::nullopt;

  const double w = 1.0 - u - v;
  Vec3 normal = triangle.normals[0] * w + triangle.normals[1] * u + triangle.normals[2] * v;
  if (glm::dot(normal, normal) < EPSILON) return std::nullopt;
  normal = normalizeSafe(normal);
  // A swept body only lands while moving into the authored road side. Axis
  // probes apply their road-side check in nearestAlongAxis instead.
  if (swept && glm::dot(d, normal) >= -EPSILON) return std::nullopt;

  return CollisionHit{from + d * t, normal, t, triangleIndex,
                      triangle.surfaceId};
}
}  // namespace

TrackCollisionSurface::TrackCollisionSurface(std::vector<CollisionTriangle> triangles)
    : triangles_(std::move(triangles)), order_(triangles_.size()) {
  std::iota(order_.begin(), order_.end(), 0);
  if (!order_.empty()) build(0, order_.size());
}

int TrackCollisionSurface::build(std::size_t begin, std::size_t end) {
  Bounds bounds{{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::infinity()},
                {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity()}};
  Bounds centroids = bounds;
  for (std::size_t i = begin; i < end; ++i) {
    const auto& tr = triangles_[order_[i]];
    Vec3 centroid(0.0);
    for (const Vec3& p : tr.positions) {
      bounds.lo = minVec(bounds.lo, p);
      bounds.hi = maxVec(bounds.hi, p);
      centroid += p;
    }
    centroid *= 1.0 / 3.0;
    centroids.lo = minVec(centroids.lo, centroid);
    centroids.hi = maxVec(centroids.hi, centroid);
  }

  const int index = static_cast<int>(nodes_.size());
  nodes_.push_back(Node{bounds, -1, -1, begin, end - begin});
  if (end - begin <= 8) return index;

  const Vec3 extent = centroids.hi - centroids.lo;
  const int axis = extent.y > extent.x && extent.y >= extent.z ? 1 : (extent.z > extent.x ? 2 : 0);
  const std::size_t middle = begin + (end - begin) / 2;
  std::nth_element(order_.begin() + static_cast<std::ptrdiff_t>(begin),
                   order_.begin() + static_cast<std::ptrdiff_t>(middle),
                   order_.begin() + static_cast<std::ptrdiff_t>(end), [&](std::size_t a, std::size_t b) {
                     Vec3 ca(0.0), cb(0.0);
                     for (const Vec3& p : triangles_[a].positions) ca += p;
                     for (const Vec3& p : triangles_[b].positions) cb += p;
                     return component(ca, axis) < component(cb, axis);
                   });
  const int left = build(begin, middle), right = build(middle, end);
  nodes_[index].left = left;
  nodes_[index].right = right;
  nodes_[index].count = 0;
  return index;
}

void TrackCollisionSurface::querySegment(int nodeIndex, const Vec3& from, const Vec3& to,
                                         bool swept, std::optional<CollisionHit>& best) const {
  const Node& node = nodes_[nodeIndex];
  if (!segmentBounds(from, to, node.bounds)) return;
  if (node.left >= 0) {
    querySegment(node.left, from, to, swept, best);
    querySegment(node.right, from, to, swept, best);
    return;
  }
  for (std::size_t i = node.begin; i < node.begin + node.count; ++i) {
    const int triangleIndex = static_cast<int>(order_[i]);
    auto hit = intersect(triangles_[triangleIndex], triangleIndex, from, to, swept);
    if (hit && (!best || hit->t < best->t)) best = std::move(hit);
  }
}

void TrackCollisionSurface::queryAxisSegment(int nodeIndex, const Vec3& from, const Vec3& to,
                                             const Vec3& origin, const Vec3& up,
                                             std::optional<CollisionHit>& best) const {
  const Node& node = nodes_[nodeIndex];
  if (!segmentBounds(from, to, node.bounds)) return;
  if (node.left >= 0) {
    queryAxisSegment(node.left, from, to, origin, up, best);
    queryAxisSegment(node.right, from, to, origin, up, best);
    return;
  }
  for (std::size_t i = node.begin; i < node.begin + node.count; ++i) {
    const int triangleIndex = static_cast<int>(order_[i]);
    auto hit = intersect(triangles_[triangleIndex], triangleIndex, from, to, false);
    if (!hit || glm::dot(hit->normal, up) <= EPSILON) continue;
    const auto distSq = [&](const Vec3& p) { const Vec3 delta = p - origin; return glm::dot(delta, delta); };
    if (!best || distSq(hit->position) < distSq(best->position))
      best = std::move(hit);
  }
}

std::optional<CollisionHit> TrackCollisionSurface::sweep(const Vec3& from, const Vec3& to) const {
  if (nodes_.empty()) return std::nullopt;
  std::optional<CollisionHit> best;
  querySegment(0, from, to, true, best);
  return best;
}

std::optional<CollisionHit> TrackCollisionSurface::nearestAlongAxis(const Vec3& origin,
                                                                    const Vec3& axis,
                                                                    double maxDistance) const {
  if (nodes_.empty() || glm::dot(axis, axis) < EPSILON || maxDistance <= 0) return std::nullopt;
  const Vec3 up = normalizeSafe(axis);
  const Vec3 from = origin + up * maxDistance;
  const Vec3 to = origin - up * maxDistance;
  std::optional<CollisionHit> selected;
  queryAxisSegment(0, from, to, origin, up, selected);
  return selected;
}

}  // namespace tox
