// TrackMesh.hpp — renderer/physics-neutral compiled mesh placements.
#pragma once

#include <array>
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
  MeshBounds bounds;
  std::vector<MeshPolygon> polygons;
  std::vector<MeshTriangle> triangles;
  std::vector<MeshRail> rails;

  bool contains(double x, double z) const;
  bool withinBounds(double x, double z, double padding = 0.0) const;
};

struct Track;
struct TrackWarning;
void compileTrackMeshes(Track& track, std::vector<TrackWarning>& warnings);

}  // namespace tox
