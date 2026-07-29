#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "Vec3.hpp"

namespace tox {

struct CollisionTriangle {
  Vec3 positions[3];
  Vec3 normals[3];
  int surfaceId{-1};
};

struct CollisionHit {
  Vec3 position;
  Vec3 normal;
  double t{0.0};
  int triangleIndex{-1};
  int surfaceId{-1};
};

// Immutable renderer-neutral collision surface. The BVH is built once when a
// Track resource translates selected .mppmodel meshes into core triangles.
class TrackCollisionSurface {
public:
  explicit TrackCollisionSurface(std::vector<CollisionTriangle> triangles);

  const std::vector<CollisionTriangle>& triangles() const { return triangles_; }

  // Finds the closest road-facing surface around origin along +/- axis.
  std::optional<CollisionHit> nearestAlongAxis(const Vec3& origin, const Vec3& axis,
                                               double maxDistance) const;
  // Earliest one-sided hit along the moving point's segment.
  std::optional<CollisionHit> sweep(const Vec3& from, const Vec3& to) const;

public:  // exposed only so the translation unit's BVH slab helper can consume it
  struct Bounds {
    Vec3 lo, hi;
  };
  struct Node {
    Bounds bounds;
    int left{-1}, right{-1};
    std::size_t begin{0}, count{0};
  };

private:
  int build(std::size_t begin, std::size_t end);
  void querySegment(int nodeIndex, const Vec3& from, const Vec3& to, bool swept,
                    std::optional<CollisionHit>& best) const;
  void queryAxisSegment(int nodeIndex, const Vec3& from, const Vec3& to, const Vec3& origin,
                        const Vec3& up, std::optional<CollisionHit>& best) const;

  std::vector<CollisionTriangle> triangles_;
  std::vector<std::size_t> order_;
  std::vector<Node> nodes_;
};

using TrackCollisionSurfacePtr = std::shared_ptr<const TrackCollisionSurface>;

}  // namespace tox
