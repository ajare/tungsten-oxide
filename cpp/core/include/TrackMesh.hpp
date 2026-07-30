// TrackMesh.hpp — renderer/physics-neutral compiled mesh placements.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "TrackGeometry.hpp"

namespace tox {

struct MeshBounds {
  double minX{0.0}, maxX{0.0}, minZ{0.0}, maxZ{0.0};
};

struct MeshPolygon {
  int polygonId{-1};
  std::vector<Vec2d> outer;
  std::vector<std::vector<Vec2d>> holes;
};

struct MeshTriangle {
  std::array<Vec2d, 3> points;
};

struct MeshRail {
  int edgeId{-1};
  Vec2d a, b;
  double nx{0.0}, nz{0.0}, length{0.0};
};

struct MeshRegion {
  std::string id, assetId;
  double elevation{0.0}, railHeight{0.0};
  // The physics jump-clearance height Ship.cpp reads (a car above elevation + this has cleared the
  // rails) -- historically the same value as `railHeight` (which also drives the rendered rail
  // wall's extrusion height) for every region, real placed mesh assets included. Kept exactly equal
  // to `railHeight` for them (see TrackMesh.cpp's compilePlacement) so nothing changes there;
  // reservations are the only regions that set it independently (CENTRAL_RESERVATION_PLAN.md M6).
  double railClearanceHeight{0.0};
  // When true, `rails` only blocks a crossing whose movement direction has a positive component
  // along the rail's own outward normal -- the opposite direction (e.g. a car already inside a
  // Capped reservation's floor driving back out) passes through unobstructed. False (the default)
  // for every real placed mesh asset, matching slideAlongRails' original bidirectional behavior;
  // only reservations set it (M6) -- see TrackMesh.cpp's slideAlongRails.
  bool oneWayRails{false};
  MeshBounds bounds;
  std::vector<MeshPolygon> polygons;
  std::vector<MeshTriangle> triangles;
  std::vector<MeshRail> rails;

  bool contains(double x, double z) const;
  bool withinBounds(double x, double z, double padding = 0.0) const;
  // Segment form: true if the swept move from (x0,z0) to (x1,z1) could touch the padded box, even
  // when neither endpoint alone lies inside it (a fast-moving point can cross clean through a
  // region's bounds within a single frame). Use this to gate any collision check driven by a
  // per-frame displacement rather than a single sampled point.
  bool withinBounds(double x0, double z0, double x1, double z1, double padding = 0.0) const;
};

struct MeshMoveResult {
  double x{0.0}, z{0.0};
  bool hit{false};
};

std::optional<double> segmentCrossing(const Vec2d& a, const Vec2d& b, const Vec2d& c, const Vec2d& d);
MeshMoveResult slideAlongRails(const MeshRegion& region, const Vec2d& from, const Vec2d& to, Vec2d& velocity,
                               double margin, double restitution = 0.0);

struct Track;
struct TrackWarning;
void compileTrackMeshes(Track& track, std::vector<TrackWarning>& warnings);

}  // namespace tox
