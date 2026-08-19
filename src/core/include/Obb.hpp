// Obb.hpp — an oriented bounding box plus the separating-axis tests that let one be resolved
// against the baked collision BVH (docs/OBB_SHIP_COLLISION_PLAN.md Milestone 1).
//
// This is the only shape-vs-triangle math in the engine; everything else in TrackCollision.hpp is
// point/segment-vs-triangle. It is deliberately self-contained glm::dvec3 math rather than a
// Willpower.Geometry dependency, matching src/core's existing posture (root CLAUDE.md) -- and
// Willpower's own BoundingBox/BoundingCircle are 2D and axis-aligned anyway.
#pragma once

#include "TrackCollision.hpp"
#include "Vec3.hpp"

namespace tox {

// An oriented box. `axes` are assumed orthonormal and are NOT re-orthonormalized here -- that is
// the caller's responsibility. The ship's own right/up/forward basis (Physics, Ship.hpp) already
// satisfies this, which is exactly why no new orientation state is needed to build one.
struct Obb {
  Vec3 center{0.0, 0.0, 0.0};
  Vec3 axes[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  Vec3 halfExtents{0.0, 0.0, 0.0};

  // Corner `index` in [0, 8): bit k selects the + (set) or - (clear) side of axes[k].
  Vec3 corner(int index) const;
};

// Full 13-axis SAT (3 box face normals, the triangle normal, and 9 edge cross-products) against one
// collision triangle. On overlap, *outNormal (unit) and *outDepth describe the minimum translation
// vector: moving the box by outNormal * outDepth separates the two. The normal therefore points out
// of the triangle toward the box -- the direction a caller pushes the box to resolve the contact --
// and is unrelated to the triangle's authored render normal, which may face either way.
//
// The nine edge cross-products are what make this catch edge-on-edge contacts that a face-normals-
// only test reports as overlapping when they are not. Axes that degenerate to (near-)zero length --
// a box axis parallel to a triangle edge -- are skipped: the parallel case is always covered by one
// of the face-normal axes.
//
// Exact touching (zero overlap along the tightest axis) is reported as an overlap with depth 0
// rather than as a miss, so a caller that only cares about real penetration should test the depth.
// Either out-pointer may be null.
bool overlapsTriangle(const Obb& obb, const CollisionTriangle& triangle, Vec3* outNormal,
                      double* outDepth);

// Cheaper OBB-vs-AABB SAT: the 3 box axes and the 3 world axes only, with no edge cross-products.
// That makes it conservative -- it can report an overlap for a pair separated only by an edge-cross
// axis -- which is exactly what BVH node pruning wants: never miss a node, occasionally descend one
// that turns out to be empty.
bool overlapsAabb(const Obb& obb, const TrackCollisionSurface::Bounds& bounds);

}  // namespace tox
