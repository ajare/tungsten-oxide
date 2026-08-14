#include "TrackCollision.hpp"

#include "Obb.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace tox {
namespace {
constexpr double EPSILON = 1e-9;
// Above this |dot(normal, up)| a surface counts as floor/ceiling rather than wall, for
// sweepWall()'s purposes. Rails/barriers sit near 0; a road surface sits near 1.
constexpr double WALL_MAX_UP_DOT = 0.5;
const Vec3 WORLD_UP{0.0, 1.0, 0.0};

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
                                         SegmentFilter filter, std::optional<CollisionHit>& best) const {
  const Node& node = nodes_[nodeIndex];
  if (!segmentBounds(from, to, node.bounds)) return;
  if (node.left >= 0) {
    querySegment(node.left, from, to, filter, best);
    querySegment(node.right, from, to, filter, best);
    return;
  }
  const bool oneSided = filter == SegmentFilter::OneSidedAny;
  const Vec3 direction = to - from;
  for (std::size_t i = node.begin; i < node.begin + node.count; ++i) {
    const int triangleIndex = static_cast<int>(order_[i]);
    auto hit = intersect(triangles_[triangleIndex], triangleIndex, from, to, oneSided);
    if (!hit) continue;
    if (filter == SegmentFilter::TwoSidedWall) {
      // Mostly-horizontal surface: road/floor (or its underside), never a wall.
      if (std::fabs(glm::dot(hit->normal, WORLD_UP)) > WALL_MAX_UP_DOT) continue;
      // Two-sided, so the authored facing is arbitrary -- orient the contact normal to point back
      // toward the side the sweep started from. That is where the mover currently is, and so the
      // side it must be kept on. Orienting against travel direction instead looks equivalent but
      // mis-signs a *grazing* contact, where the mover slides nearly parallel to the wall and
      // dot(normal, direction) is ~0 with an essentially arbitrary sign -- a caller pushing the
      // mover "away from the wall" along that normal can then shove it straight through instead.
      const Vec3 toStart = from - hit->position;
      const double side = glm::dot(hit->normal, toStart);
      if (side < -EPSILON)
        hit->normal = -hit->normal;
      else if (side <= EPSILON && glm::dot(hit->normal, direction) > 0.0)
        hit->normal = -hit->normal;  // started exactly on the surface: fall back to opposing travel
    }
    if (!best || hit->t < best->t) best = std::move(hit);
  }
}

void TrackCollisionSurface::queryNearestSegment(int nodeIndex, const Vec3& from, const Vec3& to,
                                                const Vec3& origin, const Vec3* upFilter,
                                                std::optional<CollisionHit>& best) const {
  const Node& node = nodes_[nodeIndex];
  if (!segmentBounds(from, to, node.bounds)) return;
  if (node.left >= 0) {
    queryNearestSegment(node.left, from, to, origin, upFilter, best);
    queryNearestSegment(node.right, from, to, origin, upFilter, best);
    return;
  }
  for (std::size_t i = node.begin; i < node.begin + node.count; ++i) {
    const int triangleIndex = static_cast<int>(order_[i]);
    auto hit = intersect(triangles_[triangleIndex], triangleIndex, from, to, false);
    if (!hit) continue;
    if (upFilter && glm::dot(hit->normal, *upFilter) <= EPSILON) continue;
    const auto distSq = [&](const Vec3& p) { const Vec3 delta = p - origin; return glm::dot(delta, delta); };
    if (!best || distSq(hit->position) < distSq(best->position))
      best = std::move(hit);
  }
}

void TrackCollisionSurface::queryObbNode(int nodeIndex, const Obb& obb,
                                         std::vector<ObbContact>& out) const {
  const Node& node = nodes_[nodeIndex];
  if (!overlapsAabb(obb, node.bounds)) return;
  if (node.left >= 0) {
    queryObbNode(node.left, obb, out);
    queryObbNode(node.right, obb, out);
    return;
  }
  for (std::size_t i = node.begin; i < node.begin + node.count; ++i) {
    const int triangleIndex = static_cast<int>(order_[i]);
    const CollisionTriangle& triangle = triangles_[triangleIndex];
    Vec3 normal(0.0);
    double depth = 0.0;
    if (!overlapsTriangle(obb, triangle, &normal, &depth)) continue;
    // A zero-depth contact is exact touching, not penetration (overlapsTriangle reports it as an
    // overlap deliberately). It carries no resolution work and its MTV axis is whichever one
    // happened to tie at zero, so drop it rather than hand a caller a push of length nothing.
    if (depth <= 0.0) continue;

    Vec3 planeNormal = glm::cross(triangle.positions[1] - triangle.positions[0],
                                  triangle.positions[2] - triangle.positions[0]);
    if (glm::dot(planeNormal, planeNormal) < EPSILON) continue;  // degenerate sliver: no plane to push along
    planeNormal = normalizeSafe(planeNormal);
    double centerSide = glm::dot(obb.center - triangle.positions[0], planeNormal);
    if (centerSide < 0.0) {
      planeNormal = -planeNormal;
      centerSide = -centerSide;
    }
    // The plane is one of the SAT axes the overlap above already cleared, so the box's reach along
    // it necessarily exceeds how far its centre sits from the plane -- this is never negative.
    const double planeDepth = std::fabs(glm::dot(obb.axes[0], planeNormal)) * obb.halfExtents[0] +
                              std::fabs(glm::dot(obb.axes[1], planeNormal)) * obb.halfExtents[1] +
                              std::fabs(glm::dot(obb.axes[2], planeNormal)) * obb.halfExtents[2] - centerSide;

    out.push_back(ObbContact{normal, depth, planeNormal, planeDepth, triangleIndex, triangle.surfaceId});
  }
}

void TrackCollisionSurface::queryObb(const Obb& obb, std::vector<ObbContact>& out) const {
  out.clear();
  if (nodes_.empty()) return;
  queryObbNode(0, obb, out);
}

std::optional<CollisionHit> TrackCollisionSurface::sweep(const Vec3& from, const Vec3& to) const {
  if (nodes_.empty()) return std::nullopt;
  std::optional<CollisionHit> best;
  querySegment(0, from, to, SegmentFilter::OneSidedAny, best);
  return best;
}

std::optional<CollisionHit> TrackCollisionSurface::sweepWall(const Vec3& from, const Vec3& to) const {
  if (nodes_.empty()) return std::nullopt;
  std::optional<CollisionHit> best;
  querySegment(0, from, to, SegmentFilter::TwoSidedWall, best);
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
  queryNearestSegment(0, from, to, origin, &up, selected);
  return selected;
}

std::optional<CollisionHit> TrackCollisionSurface::nearestAcrossAxis(const Vec3& origin,
                                                                     const Vec3& axis,
                                                                     double maxDistance) const {
  if (nodes_.empty() || glm::dot(axis, axis) < EPSILON || maxDistance <= 0) return std::nullopt;
  const Vec3 dir = normalizeSafe(axis);
  const Vec3 from = origin + dir * maxDistance;
  const Vec3 to = origin - dir * maxDistance;
  std::optional<CollisionHit> selected;
  queryNearestSegment(0, from, to, origin, nullptr, selected);
  return selected;
}

}  // namespace tox
