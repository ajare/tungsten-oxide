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

struct Obb;

// One penetrating triangle from a TrackCollisionSurface::queryObb. Deliberately not a CollisionHit:
// a shape overlap has no `t` along a path and no single contact `position`, and its `normal` means
// something different -- see below.
struct ObbContact {
  // Minimum-translation-vector: moving the box by normal * depth is the SHORTEST move that
  // separates it from this triangle, whichever of the 13 SAT axes that turns out to be.
  Vec3 normal;
  double depth{0.0};
  // The triangle's plane, with the normal flipped to point toward the box's centre -- the side the
  // box is on, and so the side it must be kept on. planeDepth is how far along planeNormal the box
  // has to move to clear the plane.
  //
  // This, not the MTV above, is what a wall/floor resolver should push along. On a wall built out
  // of many triangles, a box overlapping one triangle near its interior edge has a *shorter* way
  // out sideways across that edge than straight back out of the wall face, so the true MTV
  // regularly points along an edge-cross axis into the neighbouring triangle -- the classic
  // internal-edge artifact. Pushing along the surface's own plane instead keeps the resolution
  // consistent across a triangulated wall, and a triangle the box merely brushes still resolves as
  // "back out of that surface".
  //
  // planeNormal is geometric (from the triangle's own vertices), not the authored vertex normals
  // the segment queries interpolate: a discrete overlap has no single contact point at which to
  // interpolate them, and the classification callers want -- wall vs floor -- is a fact about the
  // actual triangle, not about how it happens to be shaded.
  Vec3 planeNormal;
  double planeDepth{0.0};
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
  // Finds the closest surface around origin along +/- axis, of any orientation -- unlike
  // nearestAlongAxis, a hit isn't required to face back along the probe direction. Meant for
  // lateral/wall probes, where the ship may be on either side of (or inside) the collidable
  // geometry, so no single "facing" direction is assumable up front.
  std::optional<CollisionHit> nearestAcrossAxis(const Vec3& origin, const Vec3& axis,
                                                double maxDistance) const;
  // Earliest one-sided hit along the moving point's segment.
  std::optional<CollisionHit> sweep(const Vec3& from, const Vec3& to) const;
  // Every triangle an oriented box currently overlaps, as minimum-translation-vector contacts
  // (docs/OBB_SHIP_COLLISION_PLAN.md Milestone 2). Unlike the segment queries above this is a
  // *discrete* test at one fixed pose -- it reports the overlap a box already has, not the one it
  // is about to acquire -- so a caller stepping a fast body has to substep small enough that the
  // box can't cross thin geometry between two poses.
  //
  // Takes its output vector rather than returning one: the wall-collision path calls this several
  // times per physics substep, and reusing one caller-owned buffer keeps that allocation-free.
  // `out` is cleared first. Contacts come out in BVH traversal order, not sorted by depth.
  void queryObb(const Obb& obb, std::vector<ObbContact>& out) const;

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
  // Earliest hit the segment moves *into* (dot(d, normal) < 0), of any orientation: sweep()'s
  // behavior. A two-sided, floor-excluding wall variant lived here until hull-OBB collision
  // replaced the ship's line-based wall probes outright (docs/OBB_SHIP_COLLISION_PLAN.md) --
  // queryObb is what answers "what walls am I touching" now.
  void querySegment(int nodeIndex, const Vec3& from, const Vec3& to,
                    std::optional<CollisionHit>& best) const;
  // upFilter == nullptr accepts a hit of any orientation; otherwise only hits whose interpolated
  // normal points into *upFilter (used by nearestAlongAxis's road-facing requirement).
  void queryNearestSegment(int nodeIndex, const Vec3& from, const Vec3& to, const Vec3& origin,
                           const Vec3* upFilter, std::optional<CollisionHit>& best) const;
  void queryObbNode(int nodeIndex, const Obb& obb, std::vector<ObbContact>& out) const;

  std::vector<CollisionTriangle> triangles_;
  std::vector<std::size_t> order_;
  std::vector<Node> nodes_;
};

using TrackCollisionSurfacePtr = std::shared_ptr<const TrackCollisionSurface>;

}  // namespace tox
