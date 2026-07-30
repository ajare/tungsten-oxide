// track_tests.cpp — focused native track loading/bake/geometry/mesh tests.
// M2 validates strict current-schema loading and reference-produced normalization
// summaries. Later milestones add bake and physics assertions to this target.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include "GameSession.hpp"
#include "ShipFactory.hpp"
#include "Simulation.hpp"
#include "StartGrid.hpp"
#include "Track.hpp"
#include "nlohmann/json.hpp"

using nlohmann::json;
using namespace tox;

namespace {

int failures = 0;
double worstOracleDelta = 0.0, worstOracleRatio = 0.0;
std::string worstOracleField;

void check(bool condition, const std::string& message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

json readJson(const std::filesystem::path& path) {
  std::ifstream input(path);
  json value;
  input >> value;
  return value;
}

std::string dump(const json& value) { return value.dump(); }

json normalizedSummary(const TrackDefinition& track) {
  std::size_t pointCount = 0, vertexCount = 0, edgeCount = 0, railCount = 0, polygonCount = 0;
  for (const auto& path : track.paths) pointCount += path.points.size();
  for (const auto& [id, asset] : track.meshAssets) {
    (void)id;
    vertexCount += asset.vertices.size();
    edgeCount += asset.edges.size();
    polygonCount += asset.polygons.size();
    for (const auto& edge : asset.edges)
      if (edge.rail) ++railCount;
  }

  json placements = json::array();
  for (const auto& mesh : track.meshes)
    placements.push_back({{"id", mesh.id}, {"asset", mesh.assetId}, {"x", mesh.x}, {"z", mesh.z}, {"rotation", mesh.rotation}, {"elevation", mesh.elevation}});
  json effects = json::array();
  for (const auto& zone : track.zones)
    effects.push_back({{"id", zone.id}, {"effect", zone.effect}, {"kind", zone.host.kind}});
  json gates = json::array();
  for (const auto& trigger : track.triggers)
    gates.push_back({{"id", trigger.id}, {"type", trigger.type}, {"role", trigger.role}, {"direction", trigger.direction}, {"kind", trigger.host.kind}});

  return {{"name", track.name},
          {"samples", track.samples},
          {"paths", track.paths.size()},
          {"points", pointCount},
          {"meshAssets", track.meshAssets.size()},
          {"meshes", track.meshes.size()},
          {"vertices", vertexCount},
          {"edges", edgeCount},
          {"railEdges", railCount},
          {"polygons", polygonCount},
          {"zones", track.zones.size()},
          {"triggers", track.triggers.size()},
          {"start", {{"path", track.start.path}, {"point", track.start.point}, {"reverse", track.start.reverse}}},
          {"handling",
           {{"maxSpeed", track.handling.maxSpeed}, {"accel", track.handling.accel}, {"turnSpeed", track.handling.turnSpeed}, {"weight", track.handling.weight}}},
          {"placements", std::move(placements)},
          {"effects", std::move(effects)},
          {"gates", std::move(gates)}};
}

void checkClose(double got, double want, double tolerance, const std::string& message) {
  const double delta = std::fabs(got - want);
  const double ratio = tolerance > 0 ? delta / tolerance : delta;
  if (ratio > worstOracleRatio) {
    worstOracleDelta = delta;
    worstOracleRatio = ratio;
    worstOracleField = message;
  }
  check(std::isfinite(got) && delta <= tolerance,
        message + ": got " + std::to_string(got) + ", want " + std::to_string(want));
}

void checkVec(const Vec3& got, const json& want, double tolerance, const std::string& message) {
  checkClose(got.x, want[0].get<double>(), tolerance, message + ".x");
  checkClose(got.y, want[1].get<double>(), tolerance, message + ".y");
  checkClose(got.z, want[2].get<double>(), tolerance, message + ".z");
}

void checkVec2(const Vec2d& got, const json& want, double tolerance, const std::string& message) {
  checkClose(got.x, want[0].get<double>(), tolerance, message + ".x");
  checkClose(got.y, want[1].get<double>(), tolerance, message + ".z");
}

// Routes through the canonical ShipFactory::makeShip (NATIVE_GAME_RUNTIME_PLAN.md
// §2.1) at an arbitrary test position/orientation, so ad hoc test ships get the
// same handling/race initialization a real roster ship would.
Ship shipAt(const Simulation& simulation, const Track& track, const Vec3& position, const Vec3& forward = Vec3(0, 0, 1)) {
  return ShipFactory::makeShip(simulation, track, Pose{position, UP, forward});
}

double triangleArea(const MeshRegion& region) {
  double area = 0;
  for (const auto& triangle : region.triangles) {
    const auto& a = triangle.points[0];
    const auto& b = triangle.points[1];
    const auto& c = triangle.points[2];
    area += std::fabs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / 2;
  }
  return area;
}

bool hasWarning(const TrackLoadResult& result, const std::string& code) {
  for (const auto& warning : result.warnings)
    if (warning.code == code) return true;
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: track_tests <mesh-fixture-directory>\n";
    return 2;
  }

  const std::filesystem::path fixtureDir = argv[1];
  check(std::filesystem::is_directory(fixtureDir), "fixture directory exists");

  const std::set<std::string> expectedFiles{
      "concave-railed-pad.json", "corridor-mesh-bridge.json", "mesh-effects.json",
      "overlapping-elevations.json", "pad-with-hole.json", "shared-seam.json",
      "transformed-square.json"};
  const json expectedSummaries = readJson(fixtureDir / "expected" / "normalized-summary.json");
  const json expectedCompiled = readJson(fixtureDir / "expected" / "compiled-summary.json");
  std::set<std::string> found;

  if (std::filesystem::is_directory(fixtureDir)) {
    for (const auto& entry : std::filesystem::directory_iterator(fixtureDir)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      const std::string label = entry.path().filename().string();
      found.insert(label);

      const TrackLoadResult loaded = Track::fromFile(entry.path());
      check(static_cast<bool>(loaded), label + " loads through Track::fromFile: " + loaded.error);
      if (!loaded) continue;
      check(loaded.warnings.empty(), label + " loads without warnings");
      check(loaded.track->definition.version == TrackCore::TRACK_SCHEMA_VERSION, label + " normalizes to current schema " + std::to_string(TrackCore::TRACK_SCHEMA_VERSION));
      check(!loaded.track->definition.paths.empty(), label + " has at least one path");
      check(!loaded.track->definition.meshAssets.empty(), label + " has mesh assets");
      check(!loaded.track->definition.meshes.empty(), label + " has mesh placements");

      const json actual = normalizedSummary(loaded.track->definition);
      check(expectedSummaries.contains(label), label + " has a reference normalization summary");
      if (expectedSummaries.contains(label))
        check(actual == expectedSummaries.at(label), label + " normalized summary matches reference\n  got:  " + dump(actual) +
                                                         "\n  want: " + dump(expectedSummaries.at(label)));

      check(expectedCompiled.contains(label), label + " has a reference compiled mesh summary");
      if (!expectedCompiled.contains(label)) continue;
      const json& expectedRegions = expectedCompiled.at(label);
      check(loaded.track->meshRegions.size() == expectedRegions.size(), label + " compiles every mesh placement");
      for (std::size_t i = 0; i < std::min(loaded.track->meshRegions.size(), expectedRegions.size()); ++i) {
        const MeshRegion& region = loaded.track->meshRegions[i];
        const json& expected = expectedRegions[i];
        check(region.id == expected["id"].get<std::string>() && region.assetId == expected["assetId"].get<std::string>(),
              label + " preserves placement and asset identity");
        checkClose(region.elevation, expected["elevation"].get<double>(), 1e-12, label + " elevation");
        checkClose(region.railHeight, expected["railHeight"].get<double>(), 1e-12, label + " rail height");
        checkClose(region.bounds.minX, expected["bounds"]["minX"].get<double>(), 2e-12, label + " bounds.minX");
        checkClose(region.bounds.maxX, expected["bounds"]["maxX"].get<double>(), 2e-12, label + " bounds.maxX");
        checkClose(region.bounds.minZ, expected["bounds"]["minZ"].get<double>(), 2e-12, label + " bounds.minZ");
        checkClose(region.bounds.maxZ, expected["bounds"]["maxZ"].get<double>(), 2e-12, label + " bounds.maxZ");
        check(region.polygons.size() == expected["polygons"].size(), label + " polygon count matches reference");
        for (std::size_t p = 0; p < std::min(region.polygons.size(), expected["polygons"].size()); ++p) {
          check(region.polygons[p].polygonId == expected["polygons"][p]["polygonId"].get<int>(), label + " polygon keeps authored id");
          check(region.polygons[p].outer.size() == expected["polygons"][p]["outerCount"].get<std::size_t>(), label + " outer-loop topology matches reference");
          check(region.polygons[p].holes.size() == expected["polygons"][p]["holeCounts"].size(), label + " hole count matches reference");
          for (std::size_t h = 0; h < std::min(region.polygons[p].holes.size(), expected["polygons"][p]["holeCounts"].size()); ++h)
            check(region.polygons[p].holes[h].size() == expected["polygons"][p]["holeCounts"][h].get<std::size_t>(),
                  label + " hole-loop topology matches reference");
        }
        check(region.triangles.size() == expected["triangleCount"].get<std::size_t>(), label + " equivalent triangulation count matches reference");
        checkClose(triangleArea(region), expected["triangleArea"].get<double>(), 2e-9, label + " triangulated area");
        check(region.rails.size() == expected["rails"].size(), label + " rail count matches reference");
        for (std::size_t r = 0; r < std::min(region.rails.size(), expected["rails"].size()); ++r) {
          const MeshRail& rail = region.rails[r];
          const json& expectedRail = expected["rails"][r];
          check(rail.edgeId == expectedRail["edgeId"].get<int>(), label + " rail keeps authored edge id");
          checkVec2(rail.a, expectedRail["a"], 2e-12, label + " rail.a");
          checkVec2(rail.b, expectedRail["b"], 2e-12, label + " rail.b");
          checkClose(rail.nx, expectedRail["normal"][0].get<double>(), 2e-12, label + " rail.nx");
          checkClose(rail.nz, expectedRail["normal"][1].get<double>(), 2e-12, label + " rail.nz");
          checkClose(rail.length, expectedRail["length"].get<double>(), 2e-12, label + " rail.length");
        }

        const std::string surfaceId = "mesh-" + region.id + "-surface";
        const std::string railId = "mesh-" + region.id + "-rails";
        const auto surface = std::find_if(loaded.track->geometry.begin(), loaded.track->geometry.end(),
                                          [&](const auto& batch) { return batch.id == surfaceId; });
        const auto rails = std::find_if(loaded.track->geometry.begin(), loaded.track->geometry.end(),
                                        [&](const auto& batch) { return batch.id == railId; });
        check(surface != loaded.track->geometry.end() && surface->kind == GeometryKind::MeshSurface,
              label + " emits mesh surface geometry");
        check(rails != loaded.track->geometry.end() && rails->kind == GeometryKind::MeshRail,
              label + " emits mesh rail geometry");
        if (surface != loaded.track->geometry.end()) {
          check(surface->vertices.size() == region.triangles.size() * 3 && surface->indices.size() == region.triangles.size() * 3,
                label + " surface geometry covers every Willpower triangle");
          check(surface->materialKey == "Tracks/DefaultMeshMaterial" && !surface->hasUv && !surface->texture,
                label + " mesh surface keeps renderer-neutral material metadata");
        }
        if (rails != loaded.track->geometry.end()) {
          check(rails->vertices.size() == region.rails.size() * 6 && rails->indices.size() == region.rails.size() * 6,
                label + " rail geometry covers every compiled rail");
          check(rails->materialKey == "Tracks/DefaultRailMaterial" && !rails->hasUv && !rails->texture,
                label + " mesh rails keep renderer-neutral material metadata");
        }
        for (const auto batch : {surface, rails}) {
          if (batch == loaded.track->geometry.end()) continue;
          for (std::uint32_t index : batch->indices) check(index < batch->vertices.size(), label + " mesh geometry index is valid");
          for (const auto& vertex : batch->vertices) {
            check(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) && std::isfinite(vertex.position.z) &&
                      std::isfinite(vertex.normal.x) && std::isfinite(vertex.normal.y) && std::isfinite(vertex.normal.z),
                  label + " mesh render attributes are finite");
            check(vertex.rgba.r == 1 && vertex.rgba.g == 1 && vertex.rgba.b == 1 && vertex.rgba.a == 1,
                  label + " mesh render RGBA is opaque white");
          }
        }
      }
    }
  }

  check(found == expectedFiles, "fixture inventory matches the seven shared M0 cases");

  // M4 semantic queries: holes subtract, concavity remains outside, shared
  // polygon seams are not rails, and bounds padding follows the reference contract.
  const auto holeTrack = Track::fromFile(fixtureDir / "pad-with-hole.json");
  if (holeTrack && !holeTrack.track->meshRegions.empty()) {
    const MeshRegion& region = holeTrack.track->meshRegions[0];
    check(region.contains(-20, -20), "solid area of holed mesh contains world point");
    check(!region.contains(0, 0), "polygon hole excludes its world point");
    check(!region.contains(40, 0), "outside point is not contained");
    check(region.withinBounds(30.5, 0, 0.5) && !region.withinBounds(30.5, 0), "bounds query honors padding");
    check(region.withinBounds(-100, 0, 100, 0), "segment bounds query catches a fast sweep tunneling clean through the box");
    check(!region.withinBounds(-100, 100, 100, 100), "segment bounds query still rejects a sweep that misses the box entirely");
  }
  const auto concaveTrack = Track::fromFile(fixtureDir / "concave-railed-pad.json");
  if (concaveTrack && !concaveTrack.track->meshRegions.empty()) {
    const MeshRegion& region = concaveTrack.track->meshRegions[0];
    check(region.contains(-20, 10), "concave mesh contains its lower arm");
    check(!region.contains(10, 40), "concave cutout remains outside");
  }
  const auto seamTrack = Track::fromFile(fixtureDir / "shared-seam.json");
  if (seamTrack && !seamTrack.track->meshRegions.empty())
    check(seamTrack.track->meshRegions[0].rails.size() == 6, "shared interior polygon edge is not railed");

  // M5: native mesh ownership, collision, transitions, landing and effects.
  const auto overlapTrack = Track::fromFile(fixtureDir / "overlapping-elevations.json");
  if (overlapTrack) {
    Simulation simulation(*overlapTrack.track);
    const Sample upperSample = simulation.sampleTrack(0, 11, 0);
    const MeshRegion* upper = simulation.surfaceOwnerAt(0, 0, 11, upperSample);
    const Sample lowerSample = simulation.sampleTrack(0, 1, 0);
    const MeshRegion* lower = simulation.surfaceOwnerAt(0, 0, 1, lowerSample);
    check(upper && upper->id == "upper-deck", "surface ownership selects nearest upper mesh");
    check(lower && lower->id == "lower-deck", "surface ownership selects nearest lower mesh");

    json lowered = readJson(fixtureDir / "overlapping-elevations.json");
    lowered["meshes"][0]["elevation"] = -35;
    const auto loweredTrack = Track::fromJson(lowered.dump());
    check(loweredTrack && loweredTrack.track->trackFloorY == -135,
          "lowest mesh elevation contributes to respawn floor");
  }
  if (holeTrack) {
    const Track& track = *holeTrack.track;
    Simulation simulation(track);
    Ship ship = shipAt(simulation, track, {20, 4, -25}, {0, 0, -1});
    ship.physics.speed = 60;
    simulation.stepPhysics(ship, 0.1, 0, 0, 0);
    check(!ship.physics.airborne && ship.physics.groundPos.z > -30 && ship.physics.moveDir.z > 0,
          "grounded mesh rail stops and reflects ship");

    auto airborneRun = [&](double y) {
      Ship airborne = shipAt(simulation, track, {20, y, -35});
      airborne.physics.airborne = true;
      airborne.physics.speed = 80;
      airborne.physics.moveDir.set(0, 0, 1);
      simulation.stepPhysics(airborne, 0.1, 0, 0, 0);
      return airborne;
    };
    const Ship blocked = airborneRun(6);
    const Ship cleared = airborneRun(12);
    check(blocked.physics.groundPos.z < -29, "airborne ship below rail top is blocked from outside");
    check(cleared.physics.groundPos.z > -30, "airborne ship above rail top clears wall");

    auto landingRun = [&](double x) {
      Ship airborne = shipAt(simulation, track, {x, 7, 0});
      airborne.physics.airborne = true;
      airborne.physics.verticalVel = -15;
      airborne.physics.speed = 0;
      simulation.stepPhysics(airborne, 0.15, 0, 0, 0);
      return airborne;
    };
    const Ship solidLanding = landingRun(20);
    const Ship holeLanding = landingRun(0);
    check(!solidLanding.physics.airborne && solidLanding.physics.groundPos.y == 4,
          "airborne ship lands on solid mesh polygon");
    check(holeLanding.physics.airborne, "airborne ship does not land in polygon hole");
  }
  {
    json transformed = readJson(fixtureDir / "transformed-square.json");
    transformed["meshes"][0]["x"] = 100;
    transformed["meshes"][0]["z"] = 0;
    transformed["meshes"][0]["rotation"] = 0;
    transformed["meshes"][0]["elevation"] = 8;
    const auto loaded = Track::fromJson(transformed.dump());
    if (loaded) {
      Simulation simulation(*loaded.track);
      Ship ship = shipAt(simulation, *loaded.track, {120, 8, 35});
      ship.physics.speed = 60;
      simulation.stepPhysics(ship, 0.1, 0, 0, 0);
      check(ship.physics.airborne && ship.physics.groundPos.z > 40,
            "crossing an unrailed mesh edge launches ship when no corridor receives it");
    }
  }
  {
    json overlap = readJson(fixtureDir / "overlapping-elevations.json");
    overlap["meshes"][1]["x"] = -55;
    overlap["meshes"][1]["z"] = -35;
    overlap["meshes"][1]["rotation"] = 0;
    const auto loaded = Track::fromJson(overlap.dump());
    if (loaded) {
      Simulation simulation(*loaded.track);
      Ship ship = shipAt(simulation, *loaded.track, {10, 12, 0}, {1, 0, 0});
      ship.physics.speed = 80;
      simulation.stepPhysics(ship, 0.1, 0, 0, 0);
      check(!ship.physics.airborne && ship.physics.groundPos.y == 0,
            "leaving upper mesh transfers directly to overlapping lower mesh");
    }
  }
  {
    const auto bridge = Track::fromFile(fixtureDir / "corridor-mesh-bridge.json");
    if (bridge) {
      Simulation simulation(*bridge.track);
      Ship ship = shipAt(simulation, *bridge.track, {0, 0, 35});
      ship.physics.speed = 80;
      simulation.stepPhysics(ship, 0.1, 0, 0, 0);
      check(!ship.physics.airborne && ship.physics.groundPos.z > 40,
            "leaving mesh transfers to underlying corridor");
    }
  }
  {
    const auto effects = Track::fromFile(fixtureDir / "mesh-effects.json");
    if (effects) {
      const MeshRegion& arena = effects.track->meshRegions[0];
      Vec2d elasticVelocity{0, -20};
      const MeshMoveResult elastic = slideAlongRails(arena, {0, -40}, {0, -60}, elasticVelocity,
                                                     TrackCore::COLLISION_WALL_MARGIN, 1);
      check(elastic.hit && elastic.z >= -50 + TrackCore::COLLISION_WALL_MARGIN - 1e-9 &&
                std::fabs(elasticVelocity.y - 20) < 1e-9,
            "head-on mesh rail sweep applies restitution without tunneling");
      Vec2d glancingVelocity{20, -5};
      const MeshMoveResult glancing = slideAlongRails(arena, {-10, -48}, {10, -52}, glancingVelocity,
                                                      TrackCore::COLLISION_WALL_MARGIN);
      check(glancing.hit && glancing.x > -10 && std::fabs(glancingVelocity.x - 20) < 1e-9,
            "glancing mesh rail sweep preserves tangential velocity");
      Vec2d fastVelocity{0, -500};
      const MeshMoveResult fast = slideAlongRails(arena, {0, -40}, {0, -400}, fastVelocity,
                                                  TrackCore::COLLISION_WALL_MARGIN);
      check(fast.hit && fast.z >= -50 + TrackCore::COLLISION_WALL_MARGIN - 1e-9,
            "high-speed mesh rail sweep cannot tunnel");
      Vec2d outsideVelocity{0, 20};
      const MeshMoveResult outside = slideAlongRails(arena, {0, -60}, {0, -40}, outsideVelocity,
                                                     TrackCore::COLLISION_WALL_MARGIN);
      check(outside.hit && !arena.contains(outside.x, outside.z),
            "mesh rail blocks an approach from outside");
      Vec2d cornerVelocity{-20, -20};
      const MeshMoveResult corner = slideAlongRails(arena, {-45, -45}, {-55, -55}, cornerVelocity,
                                                    TrackCore::COLLISION_WALL_MARGIN);
      check(corner.hit && corner.x >= -50 + TrackCore::COLLISION_WALL_MARGIN - 1e-9 &&
                corner.z >= -50 + TrackCore::COLLISION_WALL_MARGIN - 1e-9,
            "mesh rail sweep resolves both walls of a corner");

      const auto zone = std::find_if(effects.track->zones.begin(), effects.track->zones.end(),
                                     [](const Zone& value) { return value.id == "mesh-boost"; });
      const auto trigger = std::find_if(effects.track->triggers.begin(), effects.track->triggers.end(),
                                        [](const Trigger& value) { return value.id == "mesh-finish"; });
      check(zone != effects.track->zones.end() && zone->kind == "mesh" && zone->hostRegionIndex == 0,
            "mesh-hosted boost compiles against native region");
      const auto zoneGeometry = std::find_if(effects.track->geometry.begin(), effects.track->geometry.end(),
                                             [](const GeometryBatch& batch) { return batch.id == "zone-mesh-boost"; });
      check(zoneGeometry != effects.track->geometry.end() && zoneGeometry->kind == GeometryKind::ZoneSurface &&
                zoneGeometry->vertices.size() == 6 && zoneGeometry->hasUv,
            "mesh-hosted zone emits renderer-neutral rectangle geometry");
      check(trigger != effects.track->triggers.end() && trigger->center.y == 5,
            "mesh-hosted trigger compiles at region elevation");
      Simulation simulation(*effects.track);
      Ship ship = shipAt(simulation, *effects.track, {0, 5, 0});
      Ship otherShip = shipAt(simulation, *effects.track, {40, 5, 0});
      simulation.stepPhysics(ship, 1.0 / 120.0, 0, 0, 0);
      simulation.stepPhysics(otherShip, 1.0 / 120.0, 0, 0, 0);
      check(ship.physics.boostActive && ship.zoneInside["mesh-boost"] &&
                !otherShip.physics.boostActive && !otherShip.zoneInside["mesh-boost"],
            "mesh-hosted boost uses independent per-ship zone state");
      ship.prevTriggerPos.set(0, 5, 19);
      ship.physics.groundPos.set(0, 5, 21);
      simulation.detectTriggers(ship, ship.prevTriggerPos, ship.physics.groundPos);
      check(ship.lastCheckpoint.valid && ship.lastCheckpoint.triggerId == "mesh-finish" &&
                !otherShip.lastCheckpoint.valid,
            "mesh-hosted checkpoint uses independent generic trigger state");

      // NATIVE_GAME_RUNTIME_PLAN.md §2.6: onTriggerFired/now surface a
      // TriggerFired notice for every crossing, and an additional
      // CheckpointAccepted/LapCompleted notice for a checkpoint crossing that
      // advances the race. "mesh-finish" has role finish and zero
      // intermediates, so one crossing both fires and immediately laps.
      std::vector<TriggerNotice> notices;
      Simulation noticeSim(*effects.track);
      noticeSim.now = [] { return 42.0; };
      noticeSim.onTriggerFired = [&](Ship&, const Trigger&, const std::string&, TriggerNotice notice) {
        notices.push_back(notice);
      };
      Ship lapShip = shipAt(noticeSim, *effects.track, {0, 5, 0});
      lapShip.prevTriggerPos.set(0, 5, 19);
      lapShip.physics.groundPos.set(0, 5, 21);
      noticeSim.detectTriggers(lapShip, lapShip.prevTriggerPos, lapShip.physics.groundPos);
      check(notices.size() == 2 && notices[0] == TriggerNotice::Fired && notices[1] == TriggerNotice::LapCompleted,
            "fireTrigger notifies Fired then LapCompleted for a finish crossing with no intermediates");
      check(lapShip.race.laps == 1 && lapShip.race.lapStartedAt == 42.0 &&
                lapShip.race.flashUntil == 42.0 + Consts::CHECKPOINT_FLASH_MS,
            "fireTrigger seeds the deterministic lap clock from the injected now()");
    }
  }

  // M3: independently baked curved/banked path against selected reference fixture data.
  const std::filesystem::path pathFixture = fixtureDir.parent_path() / "path" / "curved-banked.json";
  const json pathExpected = readJson(fixtureDir.parent_path() / "path" / "expected" / "curved-banked-summary.json");
  const TrackLoadResult pathLoaded = Track::fromFile(pathFixture);
  check(static_cast<bool>(pathLoaded), "curved/banked current-schema path loads and bakes: " + pathLoaded.error);
  if (pathLoaded) {
    const Track& track = *pathLoaded.track;
    check(track.paths.size() == 1, "native bake produces one path");
    const Path& path = track.paths[0];
    const json& expectedPath = pathExpected["paths"][0];
    check(path.centerline.size() == expectedPath["frameCount"].get<std::size_t>(), "adaptive physics frame count matches reference");
    check(path.anchors.size() == expectedPath["anchors"].size(), "anchor count matches reference");
    for (std::size_t i = 0; i < path.anchors.size(); ++i) checkVec(path.anchors[i], expectedPath["anchors"][i], 1e-12, "anchor");
    for (const auto& expectedFrame : expectedPath["frames"]) {
      const int index = expectedFrame["index"].get<int>();
      const Frame& frame = path.centerline[index];
      checkVec(frame.pos, expectedFrame["pos"], 2e-11, "frame.pos");
      checkVec(frame.tangent, expectedFrame["tangent"], 2e-11, "frame.tangent");
      checkVec(frame.edgeRight, expectedFrame["edgeRight"], 2e-11, "frame.edgeRight");
      checkVec(frame.normal, expectedFrame["normal"], 2e-11, "frame.normal");
      checkClose(frame.sLeft, expectedFrame["sLeft"], 2e-10, "frame.sLeft");
      checkClose(frame.sRight, expectedFrame["sRight"], 2e-10, "frame.sRight");
      checkClose(frame.crossSectionCurvature, expectedFrame["curvature"], 2e-12, "frame.curvature");
      checkClose(frame.crossSectionTightness, expectedFrame["tightness"], 2e-12, "frame.tightness");
    }
    checkClose(track.trackFloorY, pathExpected["trackFloorY"], 2e-10, "track floor");
    check(track.zones.size() == 1 && track.zones[0].id == "curve-boost", "path zone compiles");
    if (!track.zones.empty()) {
      checkClose(track.zones[0].gLo, pathExpected["zones"][0]["gLo"], 2e-10, "zone.gLo");
      checkClose(track.zones[0].gHi, pathExpected["zones"][0]["gHi"], 2e-10, "zone.gHi");
    }
    check(track.triggers.size() == 1 && track.triggers[0].id == "curve-finish", "path trigger compiles");
    if (!track.triggers.empty()) {
      checkVec(track.triggers[0].center, pathExpected["triggers"][0]["center"], 2e-10, "trigger.center");
      checkVec(track.triggers[0].right, pathExpected["triggers"][0]["right"], 2e-11, "trigger.right");
      checkVec(track.triggers[0].up, pathExpected["triggers"][0]["up"], 2e-11, "trigger.up");
      checkVec(track.triggers[0].fwd, pathExpected["triggers"][0]["fwd"], 2e-11, "trigger.fwd");

      const Trigger& trigger = track.triggers[0];
      const auto triggerGeometry = std::find_if(track.geometry.begin(), track.geometry.end(),
                                                [&](const auto& batch) { return batch.id == "trigger-" + trigger.id; });
      check(triggerGeometry != track.geometry.end(), "path trigger emits renderer-neutral geometry");
      if (triggerGeometry != track.geometry.end()) {
        int uZeroCount = 0, uOneCount = 0;
        for (const auto& vertex : triggerGeometry->vertices) {
          const double lateral = vertex.position.clone().sub(trigger.center).dot(trigger.right);
          if (vertex.uv.x == 0) {
            ++uZeroCount;
            checkClose(lateral, trigger.halfWidth, 1e-10,
                       "trigger U=0 lies on left-hand edge relative to track direction");
          } else if (vertex.uv.x == 1) {
            ++uOneCount;
            checkClose(lateral, -trigger.halfWidth, 1e-10,
                       "trigger U=1 lies on right-hand edge relative to track direction");
          }
        }
        check(uZeroCount == 3 && uOneCount == 3, "trigger quad assigns U endpoints to all six vertices");
      }
    }
    Simulation simulation(track);
    Ship ship;
    const Frame& startFrame = path.centerline.front();
    Sample startSample;
    startSample.pos = startFrame.pos;
    startSample.tangent = startFrame.tangent;
    startSample.edgeRight = startFrame.edgeRight;
    startSample.normal = startFrame.normal;
    startSample.sLeft = startFrame.sLeft;
    startSample.sRight = startFrame.sRight;
    startSample.crossSectionCurvature = startFrame.crossSectionCurvature;
    startSample.crossSectionTightness = startFrame.crossSectionTightness;
    const SurfaceFrame startSurface = curvedSurfaceFrame(startSample, 0);
    simulation.placeShipAtPose(ship, Pose{startSurface.pos, startSurface.normal, startFrame.tangent}, {});
    const StepResult step = simulation.stepPhysics(ship, 1.0 / 120.0, 1, 0, 0);
    check(!step.respawned && !ship.physics.airborne && std::isfinite(ship.physics.groundPos.x),
          "native-loaded path immediately drives through Simulation");

    for (const auto& expectedBatch : pathExpected["geometry"]) {
      const std::string id = expectedBatch["id"].get<std::string>();
      const auto foundBatch = std::find_if(track.geometry.begin(), track.geometry.end(), [&](const auto& batch) { return batch.id == id; });
      check(foundBatch != track.geometry.end(), "render batch exists: " + id);
      if (foundBatch == track.geometry.end()) continue;
      check(!foundBatch->vertices.empty() && foundBatch->indices.size() % 3 == 0, "render batch is non-empty triangles: " + id);
      check(foundBatch->vertices.size() == expectedBatch["vertexCount"].get<std::size_t>() &&
                foundBatch->indices.size() == expectedBatch["indexCount"].get<std::size_t>(),
            "adaptive render triangle count matches reference: " + id);
      check(foundBatch->hasUv == expectedBatch["hasUv"].get<bool>(), "render UV presence matches reference: " + id);
      if (expectedBatch.contains("texture") && !expectedBatch["texture"].is_null()) {
        check(foundBatch->texture.has_value(), "render texture metadata exists: " + id);
        if (foundBatch->texture) {
          check(foundBatch->texture->assetId == expectedBatch["texture"]["assetId"].get<std::string>() &&
                    foundBatch->texture->tile == expectedBatch["texture"]["tile"].get<int>(),
                "render texture metadata matches reference: " + id);
        }
      }
      Vec3 min(1e300, 1e300, 1e300), max(-1e300, -1e300, -1e300);
      for (const auto& vertex : foundBatch->vertices) {
        min.x = std::min(min.x, vertex.position.x);
        min.y = std::min(min.y, vertex.position.y);
        min.z = std::min(min.z, vertex.position.z);
        max.x = std::max(max.x, vertex.position.x);
        max.y = std::max(max.y, vertex.position.y);
        max.z = std::max(max.z, vertex.position.z);
        check(vertex.rgba.r == 1 && vertex.rgba.g == 1 && vertex.rgba.b == 1 && vertex.rgba.a == 1, "render RGBA defaults white");
      }
      checkVec(min, expectedBatch["min"], 2e-9, id + ".min");
      checkVec(max, expectedBatch["max"], 2e-9, id + ".max");
    }

    // NATIVE_GAME_RUNTIME_PLAN.md §2.1/§2.2: starting-grid poses and the
    // ship-factory roster, exercised on a closed, reversed-start, banked path
    // with a non-uniform width (30..52) so lateral compression can engage.
    Simulation gridSim(track);
    const std::vector<Pose> poses = StartGrid::startingGridPoses(gridSim, track, 8);
    check(poses.size() == 8, "startingGridPoses produces the requested roster size");
    bool allFinite = true, allUnit = true;
    for (const Pose& pose : poses) {
      if (!std::isfinite(pose.pos.x) || !std::isfinite(pose.pos.y) || !std::isfinite(pose.pos.z)) allFinite = false;
      if (std::fabs(pose.up.length() - 1.0) > 1e-9 || std::fabs(pose.forward.length() - 1.0) > 1e-9) allUnit = false;
    }
    check(allFinite, "starting grid poses are all finite");
    check(allUnit, "starting grid pose forward/up are unit vectors");
    check(poses[0].pos.distanceTo(poses[1].pos) > 0.1, "front row grid slots are laterally offset");

    const std::vector<Ship> roster = ShipFactory::buildRoster(gridSim, track, 8);
    check(roster.size() == 8, "buildRoster produces the requested roster size");
    if (!roster.empty()) {
      check(roster[0].race.finishId == "curve-finish", "buildRoster ships derive race state from track triggers");
      const double expectedTurnRate = track.definition.handling.turnSpeed * 3.14159265358979323846 / 180.0;
      check(std::fabs(roster[0].physics.maxSpeed - track.definition.handling.maxSpeed) < 1e-12 &&
                std::fabs(roster[0].physics.turnRate - expectedTurnRate) < 1e-12,
            "buildRoster applies authored handling (maxSpeed, turnRate conversion)");
    }
  }

  if (pathLoaded) {
    // NATIVE_GAME_RUNTIME_PLAN.md §2.3/§2.4: a GameSession's frame/substep
    // orchestration and explicit-respawn handling (no autopilot/steering is
    // exercised here — that belongs to the future raw-session parity layer's
    // scripted intents, not this focused unit test).
    auto trackPtr = std::make_shared<Track>(*pathLoaded.track);
    GameSession session(trackPtr, 1);
    check(session.ships().size() == 1, "GameSession builds the requested roster size");
    if (!session.ships().empty()) {
      check(session.ships()[0].race.finishId == "curve-finish" && session.ships()[0].race.intermediateIds.empty(),
            "GameSession roster derives race state from track triggers");
    }

    const Vec3 startPos = session.ships()[0].physics.groundPos;
    std::vector<ControlIntent> idle(1);
    session.step(idle, 1.0 / 60.0);
    check(session.ships()[0].physics.groundPos.distanceTo(startPos) < 1.0,
          "an idle intent leaves a parked ship close to its starting-grid pose");
    check(session.sessionTime() > 0.0, "GameSession accumulates a deterministic session clock");

    // Drive forward long enough to leave the starting-grid pose...
    std::vector<ControlIntent> drive(1);
    drive[0].throttle = 1.0;
    for (int frame = 0; frame < 30; frame++) session.step(drive, 1.0 / 60.0);
    check(session.ships()[0].physics.groundPos.distanceTo(startPos) > 1.0,
          "throttle moves the ship away from its starting-grid pose");

    // ...then an explicit respawn should snap it back (no checkpoint reached
    // yet, so the fallback is the starting-grid pose itself).
    std::vector<ControlIntent> respawnIntent(1);
    respawnIntent[0].respawn = true;
    session.step(respawnIntent, 1.0 / 60.0);
    bool sawExplicitRespawn = false;
    for (const GameEvent& event : session.events())
      if (event.type == GameEventType::Respawned && !event.automatic) sawExplicitRespawn = true;
    check(sawExplicitRespawn, "an explicit respawn intent fires a non-automatic Respawned event");
    check(session.ships()[0].physics.groundPos.distanceTo(startPos) < 1.0,
          "an explicit respawn with no checkpoint reached returns to the starting-grid pose");
  }

  const std::filesystem::path basePath = fixtureDir / "transformed-square.json";
  json base = readJson(basePath);

  check(!Track::fromJson("{not json"), "malformed JSON is fatal");
  {
    json input = base;
    input.erase("version");
    const auto loaded = Track::fromJson(input.dump());
    check(!loaded && loaded.error.find("version is required") != std::string::npos, "missing version is explicitly fatal");
  }
  {
    json input = base;
    input["version"] = 9;
    const auto loaded = Track::fromJson(input.dump());
    check(!loaded && loaded.error.find("unsupported") != std::string::npos, "historical schema is explicitly unsupported");
  }
  {
    json input = base;
    input["version"] = 12;
    const auto loaded = Track::fromJson(input.dump());
    check(!loaded && loaded.error.find("unsupported") != std::string::npos, "a schema newer than 11 is explicitly unsupported");
  }
  {
    // CENTRAL_RESERVATION_PLAN.md M0: schema 10 (the permanently pinned fixture version, no
    // reservations field at all) still loads and normalizes with an empty reservations list.
    json input = base;
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "a plain schema-10 fixture with no reservations field loads");
    if (loaded) check(loaded.track->definition.paths[0].reservations.empty(), "no reservations field means an empty list, not an error");
  }
  {
    json input = base;
    input["version"] = 11;
    input["paths"][0]["reservations"] = json::array({{{"id", "res1"}, {"t0", 0.7}, {"t1", 0.3}, {"width", 12.0}}});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "schema 11 with a reservations array loads: " + loaded.error);
    if (loaded) {
      const auto& reservations = loaded.track->definition.paths[0].reservations;
      check(reservations.size() == 1, "one reservation is parsed");
      if (!reservations.empty()) {
        check(reservations[0].id == "res1", "reservation id round-trips");
        check(reservations[0].t0 == 0.3 && reservations[0].t1 == 0.7, "reservation t0/t1 are normalized so t0 < t1 even if authored reversed");
        check(reservations[0].width == 12.0, "reservation width round-trips");
      }
    }
  }
  {
    // Out-of-range/degenerate reservations are clamped/dropped rather than rejecting the track,
    // matching every other authored field's defensive-normalization policy in this loader.
    json input = base;
    input["version"] = 11;
    input["paths"][0]["reservations"] = json::array({
        {{"id", "zeroWidth"}, {"t0", 0.2}, {"t1", 0.4}, {"width", 0.0}},
        {{"id", "zeroSpan"}, {"t0", 0.5}, {"t1", 0.5}, {"width", 10.0}},
        {{"id", "outOfRange"}, {"t0", -0.5}, {"t1", 1.5}, {"width", 5.0}},
    });
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "reservations with degenerate values still load: " + loaded.error);
    if (loaded) {
      const auto& reservations = loaded.track->definition.paths[0].reservations;
      check(reservations.size() == 1, "zero-width/zero-span reservations are dropped, leaving only the clamped one");
      if (!reservations.empty()) check(reservations[0].t0 == 0.0 && reservations[0].t1 == 1.0, "out-of-[0,1] t0/t1 are clamped into range");
    }
  }
  {
    // CENTRAL_RESERVATION_PLAN.md M1: baking a reservation carves a gap out of the path surface
    // and produces a synthetic rails-only MeshRegion plus ReservationWall geometry.
    // "interiorMode": "uncapped" explicit here (M6 defaults an unspecified reservation to Capped,
    // which gets a real landable floor -- see the M6 block below) so this keeps testing the
    // rails-only, no-floor case its own assertion names.
    json input = base;
    input["version"] = 11;
    input["paths"][0]["reservations"] =
        json::array({{{"id", "res1"}, {"t0", 0.3}, {"t1", 0.7}, {"width", 8.0}, {"interiorMode", "uncapped"}}});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "a track with a reservation bakes: " + loaded.error);
    if (loaded) {
      const Track& track = *loaded.track;
      const auto region = std::find_if(track.meshRegions.begin(), track.meshRegions.end(),
                                       [](const MeshRegion& r) { return r.id.rfind("reservation-res1", 0) == 0; });
      check(region != track.meshRegions.end(), "the reservation gets a synthetic MeshRegion");
      if (region != track.meshRegions.end()) {
        check(region->polygons.empty() && region->triangles.empty(),
              "the reservation region has no floor -- meshRegionAt/surfaceOwnerAt can never pick it as a standing surface");
        check(!region->rails.empty(), "the reservation region has boundary rails");
        check(region->bounds.minX < region->bounds.maxX && region->bounds.minZ < region->bounds.maxZ,
              "the reservation region has a non-degenerate bounds box");
      }

      const auto wall = std::find_if(track.geometry.begin(), track.geometry.end(),
                                     [](const GeometryBatch& b) { return b.kind == GeometryKind::ReservationWall; });
      check(wall != track.geometry.end() && !wall->vertices.empty(), "a ReservationWall geometry batch is emitted");

      const auto surface = std::find_if(track.geometry.begin(), track.geometry.end(),
                                        [](const GeometryBatch& b) { return b.id == "path-0-surface"; });
      check(surface != track.geometry.end(), "the path surface batch still exists");
      if (surface != track.geometry.end() && !track.paths[0].centerline.empty()) {
        // Direct correctness check (a raw vertex-count comparison isn't meaningful here: the
        // reservation also forces extra render-mesh subdivisions across its span, which can grow
        // the surface batch even as it carves a hole out of it). At the reservation's midpoint
        // (t=0.5, mid-span of its [0.3,0.7]), the gap is at full width, so no surface vertex should
        // land near the path centerline there.
        const auto& centerline = track.paths[0].centerline;
        const Vec3& void_center = centerline[static_cast<std::size_t>(std::lround(0.5 * (centerline.size() - 1)))].pos;
        const bool anyVertexInVoid = std::any_of(surface->vertices.begin(), surface->vertices.end(), [&](const RenderVertex& v) {
          return std::hypot(v.position.x - void_center.x, v.position.z - void_center.z) < 1.5;
        });
        check(!anyVertexInVoid, "no surface vertex lands near the path centerline at the reservation's full-width midpoint");
      }
    }
  }

  {
    // CENTRAL_RESERVATION_PLAN.md M6: an unspecified reservation now defaults to Capped -- a real,
    // landable floor (polygons non-empty) and a sealed shell underside -- while an explicit
    // Uncapped one keeps the original rails-only, no-floor behavior and instead carves a matching
    // hole in the shell.
    json input = base;
    input["version"] = 11;
    input["paths"][0]["reservations"] = json::array(
        {{{"id", "capped"}, {"t0", 0.1}, {"t1", 0.3}, {"width", 8.0}},
         {{"id", "uncapped"}, {"t0", 0.5}, {"t1", 0.7}, {"width", 8.0}, {"interiorMode", "uncapped"}, {"wallHeight", 3.0}, {"railClearanceHeight", 9.0}}});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "M6 mixed capped/uncapped track bakes: " + loaded.error);
    if (loaded) {
      const Track& track = *loaded.track;
      const auto findRegion = [&](const char* prefix) {
        return std::find_if(track.meshRegions.begin(), track.meshRegions.end(),
                            [&](const MeshRegion& r) { return r.id.rfind(prefix, 0) == 0; });
      };
      const auto cappedRegion = findRegion("reservation-capped");
      check(cappedRegion != track.meshRegions.end(), "the capped reservation gets a synthetic MeshRegion");
      if (cappedRegion != track.meshRegions.end()) {
        check(!cappedRegion->polygons.empty(), "a Capped reservation's region has a real, landable floor polygon");
        check(cappedRegion->oneWayRails, "a Capped reservation's rails are one-directional");
        check(cappedRegion->railHeight > 0.0, "a Capped reservation's visual wall height is set");
      }
      const auto uncappedRegion = findRegion("reservation-uncapped");
      check(uncappedRegion != track.meshRegions.end(), "the uncapped reservation gets a synthetic MeshRegion");
      if (uncappedRegion != track.meshRegions.end()) {
        check(uncappedRegion->polygons.empty() && uncappedRegion->triangles.empty(),
              "an Uncapped reservation's region still has no floor");
        check(uncappedRegion->oneWayRails, "an Uncapped reservation's rails are one-directional too (harmless with no floor to exit)");
        check(std::fabs(uncappedRegion->railHeight - 3.0) < 1e-9, "wallHeight (visual) reads back independently");
        check(std::fabs(uncappedRegion->railClearanceHeight - 9.0) < 1e-9,
              "railClearanceHeight (physics) is independent of wallHeight, not the same value (M6)");
      }

      // The Capped reservation seals the shell's underside with new geometry; the Uncapped one
      // instead carves a matching hole in it and adds no seal.
      const bool hasCappedSeal = cappedRegion != track.meshRegions.end() &&
                                 std::any_of(track.geometry.begin(), track.geometry.end(),
                                             [&](const GeometryBatch& b) { return b.id == cappedRegion->id + "-interior-seal"; });
      check(hasCappedSeal, "a Capped reservation gets an interior-seal geometry batch closing the pit's sides");
      const bool hasUncappedSeal = uncappedRegion != track.meshRegions.end() &&
                                   std::any_of(track.geometry.begin(), track.geometry.end(),
                                               [&](const GeometryBatch& b) { return b.id == uncappedRegion->id + "-interior-seal"; });
      check(!hasUncappedSeal, "an Uncapped reservation gets no interior-seal batch -- the shaft is left open");

      const auto shell = std::find_if(track.geometry.begin(), track.geometry.end(),
                                      [](const GeometryBatch& b) { return b.id == "path-0-shell"; });
      check(shell != track.geometry.end() && !shell->vertices.empty(),
            "the path's shell batch exists (default cross-section thickness) so Uncapped had something to carve");
    }
  }

  {
    // The carved hole's *resolution and accuracy*, as opposed to its mere existence above. The
    // render bake used to force a fixed 8 subdivisions across a reservation's span, snapped to the
    // nearest raw physics sample -- so the hole's shape was quantized to a handful of rings however
    // long the span was, and its ends landed wherever a raw sample happened to be rather than on
    // t0/t1, starting the void at a blunt notch instead of closing to a point.
    //
    // Reconstruct each ring's lane-boundary pair from the ReservationWall batch, which Builder::tri
    // emits without vertex reuse as 12 vertices per ring-to-ring segment -- left(a,b,at),
    // left(at,b,bt), right(a,b,at), right(at,b,bt) -- so vertex[12k+0]/[12k+6] are ring k's
    // left/right boundary and [12k+1]/[12k+7] are ring k+1's.
    // A straight, flat, default-cross-section path so a ring's surface vertices are exactly
    // collinear with that ring's wall boundary pair, and no other part of the track can lie on that
    // line -- which makes the containment check below exact rather than a tolerance judgement.
    json input;
    input["version"] = 11;
    input["name"] = "reservation-hole-fidelity";
    json points = json::array();
    for (int i = 0; i < 8; i++) points.push_back({{"type", "position"}, {"pos", {0.0, 0.0, i * 200.0}}});
    json path = {{"id", "p0"}, {"closed", false}, {"points", points}};
    const double reservationWidth = 16.0;
    path["reservations"] = json::array({{{"id", "res1"}, {"t0", 0.3}, {"t1", 0.7}, {"width", reservationWidth}}});
    input["paths"] = json::array({path});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "a track with a wide reservation bakes: " + loaded.error);
    const auto end = loaded ? loaded.track->geometry.end() : std::vector<GeometryBatch>::const_iterator{};
    const auto wall = loaded ? std::find_if(loaded.track->geometry.begin(), end,
                                            [](const GeometryBatch& b) { return b.kind == GeometryKind::ReservationWall; })
                             : end;
    const auto surface = loaded ? std::find_if(loaded.track->geometry.begin(), end,
                                               [](const GeometryBatch& b) { return b.kind == GeometryKind::PathSurface; })
                                : end;
    if (loaded && wall != end && surface != end && wall->vertices.size() >= 12) {
      const auto& v = wall->vertices;
      const std::size_t segments = v.size() / 12;
      // Ring k's left/right gap boundary. Builder::tri emits without vertex reuse, 12 vertices per
      // ring-to-ring segment -- left(a,b,at), left(at,b,bt), right(a,b,at), right(at,b,bt) -- so
      // [12k+0]/[12k+6] are ring k's pair and [12k+1]/[12k+7] are ring k+1's.
      std::vector<std::pair<Vec3, Vec3>> ring;
      for (std::size_t k = 0; k < segments; ++k) ring.emplace_back(v[12 * k + 0].position, v[12 * k + 6].position);
      ring.emplace_back(v[12 * (segments - 1) + 1].position, v[12 * (segments - 1) + 7].position);

      std::vector<double> halfGap;
      for (const auto& r : ring) halfGap.push_back(r.first.distanceTo(r.second) / 2);
      double peak = 0;
      for (double h : halfGap) peak = std::max(peak, h);

      check(halfGap.front() < 1e-6 && halfGap.back() < 1e-6,
            "the reservation's taper closes to a point at the authored t0/t1, not a blunt notch at the nearest raw sample");
      check(std::fabs(peak - reservationWidth / 2) < 0.05, "the taper still reaches the authored full width at mid-span");
      check(halfGap.size() > 40, "ring count follows the taper rather than a fixed subdivision count");

      // The void's edge must be the per-ring boundary polyline itself -- the same kind of edge the
      // road's own sides are -- not a staircase quantized to the ring spacing. The strip used to
      // drop a sub-quad only where it was inside the gap at BOTH of a segment's rings, which left
      // the corner vertex at the *narrower* ring's gap edge sitting inside the *wider* ring's gap:
      // a step of solid road jutting into the void. So: no surface vertex may lie strictly between
      // a ring's two boundary points. Exact here because the cross-section is flat, making every
      // one of ring k's surface vertices collinear with its wall pair.
      double deepest = 0;
      for (std::size_t k = 0; k < ring.size(); ++k) {
        if (halfGap[k] < 1e-3) continue;
        const Vec3 l = ring[k].first, span = ring[k].second.clone().sub(l);
        const double lengthSq = span.lengthSq();
        for (const RenderVertex& vertex : surface->vertices) {
          const Vec3 rel = vertex.position.clone().sub(l);
          const double s = rel.dot(span) / lengthSq;
          if (s <= 0 || s >= 1) continue;
          if (rel.distanceTo(span.clone().multiplyScalar(s)) > 1e-6) continue;  // not on this ring
          deepest = std::max(deepest, std::min(s, 1 - s) * 2 * halfGap[k]);
        }
      }
      check(deepest < 1e-3,
            "no surface vertex lands inside a ring's gap -- the void's edge is a per-ring polyline, not a "
            "staircase (deepest intrusion " +
                std::to_string(deepest) + "m)");
    }
  }
  {
    json input = base;
    input["paths"] = json::array();
    check(!Track::fromJson(input.dump()), "a track with no paths is fatal");
  }
  {
    json input = base;
    input["paths"][0]["points"] = json::array(
        {{{"type", "position"}, {"pos", {0, 0, 0}}},
         {{"type", "position"}, {"pos", {0, 0, 1}}},
         {{"type", "position"}, {"pos", {0, 0, 2}}}});
    check(!Track::fromJson(input.dump()), "fewer than four position points is fatal");
  }
  {
    json input = base;
    input["meshes"][0]["asset"] = "missing";
    const auto loaded = Track::fromJson(input.dump());
    check(loaded && loaded.track->definition.meshes.empty(), "dangling mesh placement is recoverable and dropped");
    check(hasWarning(loaded, "mesh-placement-missing-asset"), "dangling mesh placement emits a structured warning");
  }
  {
    json input = base;
    input["meshAssets"]["pad"]["mesh"]["edges"][0]["vertices"][0] = 999;
    const auto loaded = Track::fromJson(input.dump());
    check(loaded && loaded.track->definition.meshes.size() == 1 && loaded.track->meshRegions.empty(),
          "topologically invalid mesh remains authored but is skipped by native compilation");
    check(hasWarning(loaded, "mesh-compile-failed"), "Willpower topology failure emits a structured warning");
  }
  {
    json input = base;
    input["meshAssets"]["pad"]["mesh"]["vertices"][0].erase("position");
    const auto loaded = Track::fromJson(input.dump());
    check(loaded && loaded.track->definition.meshAssets.empty() && loaded.track->definition.meshes.empty(),
          "invalid mesh asset and its placement are skipped");
    check(hasWarning(loaded, "mesh-asset-invalid"), "invalid mesh asset emits a structured warning");
    check(hasWarning(loaded, "mesh-placement-missing-asset"), "placement of invalid asset emits a structured warning");
  }
  {
    json input = base;
    input.erase("handling");
    input.erase("start");
    input["triggers"] = json::array();
    json positions = json::array();
    for (const auto& point : input["paths"][0]["points"])
      if (point.value("type", "position") == "position") positions.push_back(point);
    input["paths"][0]["points"] = std::move(positions);
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "current-schema path defaults normalize");
    if (loaded) {
      const auto& definition = loaded.track->definition;
      check(definition.paths[0].points.size() == 10, "roll, width, and cross-section defaults are injected");
      check(definition.handling.maxSpeed == 140 && definition.handling.accel == 71 && definition.handling.turnSpeed == 137.5 &&
                definition.handling.weight == 1000,
            "handling defaults are injected field by field");
      check(definition.start.path == 0 && definition.start.point == 0 && !definition.start.reverse, "start defaults are clamped");
      check(definition.triggers.empty(), "loader leaves authored trigger list unchanged");
      check(loaded.track->triggers.size() == 1 && loaded.track->triggers[0].role == "finish",
            "M3 baking creates the automatic Finish checkpoint");
    }
  }
  {
    json input = base;
    input["zones"].push_back({{"id", "dangling-zone"}, {"host", {{"kind", "mesh"}, {"meshId", "missing"}}}});
    input["triggers"].push_back({{"id", "dangling-trigger"}, {"host", {{"kind", "path"}, {"pathId", "missing"}}}});
    const auto loaded = Track::fromJson(input.dump());
    check(loaded && loaded.track->definition.zones.empty(), "dangling zone host is dropped");
    check(loaded && loaded.track->definition.triggers.size() == 1, "dangling trigger host is dropped without disturbing valid Finish");
  }

  {
    CollisionTriangle lower;
    lower.positions[0] = {-2, 0, -2};
    lower.positions[1] = {0, 0, 2};
    lower.positions[2] = {2, 0, -2};
    lower.normals[0] = lower.normals[1] = lower.normals[2] = {0, 1, 0};
    lower.surfaceId = 10;
    CollisionTriangle upper = lower;
    for (auto& position : upper.positions) position.y = 5;
    upper.surfaceId = 20;
    TrackCollisionSurface surface({lower, upper});
    auto lowerHit = surface.nearestAlongAxis({0, 0.2, 0}, {0, 1, 0}, 6);
    auto upperHit = surface.nearestAlongAxis({0, 4.8, 0}, {0, 1, 0}, 6);
    check(lowerHit && lowerHit->surfaceId == 10 && std::fabs(lowerHit->position.y) < 1e-12,
          "BVH contact prefers the nearest stacked lower surface");
    check(upperHit && upperHit->surfaceId == 20 && std::fabs(upperHit->position.y - 5) < 1e-12,
          "BVH contact preserves the nearest stacked upper surface");
    check(surface.sweep({0, 2, 0}, {0, -2, 0}).has_value(), "one-sided sweep lands while moving into the road normal");
    check(!surface.sweep({0, -2, 0}, {0, 2, 0}).has_value(), "one-sided sweep rejects the road underside");

    CollisionTriangle smooth = lower;
    smooth.normals[0] = {0, 1, 0};
    smooth.normals[1] = Vec3(0, 1, 1).normalize();
    smooth.normals[2] = Vec3(1, 1, 0).normalize();
    TrackCollisionSurface smoothSurface({smooth});
    auto smoothHit = smoothSurface.nearestAlongAxis({0, 0.1, 0}, {0, 1, 0}, 1);
    check(smoothHit && smoothHit->normal.y < 1.0 && smoothHit->normal.y > 0.7,
          "contact normal barycentrically interpolates exported vertex normals");
  }

  {
    auto loaded = Track::fromJson(base.dump());
    check(static_cast<bool>(loaded), "external-contact ship fixture loads");
    if (loaded) {
      Track& track = *loaded.track;
      Simulation analytical(track);
      const Pose start = StartGrid::startingGridPoses(analytical, track, 1).front();
      Vec3 right;
      right.crossVectors(start.up, start.forward).normalize();
      const Vec3 center = start.pos.clone().addScaledVector(start.up, 2.0);
      const Vec3 a = center.clone().addScaledVector(right, -20).addScaledVector(start.forward, -20);
      const Vec3 b = center.clone().addScaledVector(right, 20).addScaledVector(start.forward, -20);
      const Vec3 c = center.clone().addScaledVector(right, 20).addScaledVector(start.forward, 20);
      const Vec3 d = center.clone().addScaledVector(right, -20).addScaledVector(start.forward, 20);
      CollisionTriangle first, second;
      first.positions[0] = a;
      first.positions[1] = b;
      first.positions[2] = c;
      second.positions[0] = a;
      second.positions[1] = c;
      second.positions[2] = d;
      for (int corner = 0; corner < 3; ++corner) {
        first.normals[corner] = start.up;
        second.normals[corner] = start.up;
      }
      track.collisionSurface = std::make_shared<TrackCollisionSurface>(
          std::vector<CollisionTriangle>{first, second});
      Simulation external(track);

      Ship parked = shipAt(external, track, start.pos, start.forward);
      const StepResult parkedStep = parked.step(external, 1.0 / 120.0, 0, 0, 0);
      check(std::fabs(parked.physics.groundPos.clone().sub(center).dot(start.up)) < 1e-9 &&
                !parked.physics.airborne && parkedStep.surfaceNormal.dot(start.up) > 0.999999,
            "Ship::step makes an external triangle surface authoritative for parked contact");

      Ship falling = shipAt(external, track, center.clone().addScaledVector(start.up, 2.0), start.forward);
      falling.physics.airborne = true;
      falling.physics.verticalVel = -10.0;
      falling.prevTriggerPos = falling.physics.groundPos;
      falling.step(external, 0.15, 0, 0, 0);
      check(!falling.physics.airborne &&
                std::fabs(falling.physics.groundPos.clone().sub(center).dot(start.up)) < 1e-9,
            "Ship::step lands on an external triangle swept before the analytical road");
    }
  }

  {
    // A ship driving straight at a reservation's tapered wall (CENTRAL_RESERVATION_PLAN.md M2):
    // must not diverge, must recover speed after the bounce, and (the actual game runtime always
    // has a baked TrackCollisionSurface from an exported .mppmodel -- see the second sub-case
    // below) must not spuriously toggle airborne/grounded near the gap edge, which would mean the
    // render mesh's hole (adaptiveRenderBake's anchor-forced approximation) didn't actually line up
    // with the analytical wall (built from the same frames as of this fix -- previously the wall
    // used the fine physics centerline while the hole used a handful of anchors, and the two could
    // disagree right at the edge).
    json input;
    input["version"] = 11;
    input["name"] = "diag";
    json points = json::array();
    for (int i = 0; i < 8; i++) points.push_back({{"type", "position"}, {"pos", {0.0, 0.0, i * 200.0}}});
    json path = {{"id", "p0"}, {"closed", false}, {"points", points}};
    // A short span (10% of the path) for a steep taper -- steeper than a gradual one, more likely
    // to cause repeated re-collision if slideAlongRails behaved badly while grazing it.
    path["reservations"] = json::array({{{"id", "res1"}, {"t0", 0.45}, {"t1", 0.55}, {"width", 16.0}}});
    input["paths"] = json::array({path});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "reservation-wall diag track loads: " + loaded.error);
    if (loaded) {
      auto driveInto = [&](std::shared_ptr<Track> track) {
        Simulation sim(*track);
        Ship ship;
        const Frame& startFrame = track->paths[0].centerline.front();
        Sample startSample;
        startSample.pos = startFrame.pos;
        startSample.tangent = startFrame.tangent;
        startSample.edgeRight = startFrame.edgeRight;
        startSample.normal = startFrame.normal;
        startSample.sLeft = startFrame.sLeft;
        startSample.sRight = startFrame.sRight;
        startSample.crossSectionCurvature = startFrame.crossSectionCurvature;
        startSample.crossSectionTightness = startFrame.crossSectionTightness;
        // Offset 7 world units from centerline: inside the 8-unit-half-width gap once it's fully
        // open, so driving straight ahead (no steering) rides the widening boundary and forces a
        // real slideAlongRails hit as it sweeps past.
        const SurfaceFrame startSurface = curvedSurfaceFrame(startSample, 7.0);
        sim.placeShipAtPose(ship, Pose{startSurface.pos, startSurface.normal, startFrame.tangent}, {});
        int airborneToggleCount = 0, railHits = 0;
        bool wasAirborne = false;
        bool finite = true;
        for (int i = 0; i < 400 && finite; i++) {
          const StepResult step = ship.step(sim, 1.0 / 60.0, 1.0, 0.0, 0.0);
          if (step.railHit) ++railHits;
          if (ship.physics.airborne != wasAirborne) {
            ++airborneToggleCount;
            wasAirborne = ship.physics.airborne;
          }
          finite = std::isfinite(ship.physics.groundPos.x) && std::isfinite(ship.physics.groundPos.y) &&
                   std::isfinite(ship.physics.groundPos.z) && std::isfinite(ship.physics.speed);
        }
        return std::make_tuple(finite, railHits, airborneToggleCount, ship.physics.speed);
      };

      auto trackPtr = std::make_shared<Track>(*loaded.track);
      const auto [finite, railHits, toggles, finalSpeed] = driveInto(trackPtr);
      check(finite, "reservation-wall diag: position/speed stays finite for 400 steps");
      check(railHits >= 1, "reservation-wall diag: driving into the taper actually triggers a wall hit");
      check(toggles == 0, "reservation-wall diag: no collisionSurface means no airborne transitions at all");
      check(finalSpeed > 100.0, "reservation-wall diag: speed recovers after the bounce rather than grinding to a halt");

      const auto surfaceBatch = std::find_if(trackPtr->geometry.begin(), trackPtr->geometry.end(),
                                             [](const GeometryBatch& b) { return b.id == "path-0-surface"; });
      check(surfaceBatch != trackPtr->geometry.end(), "reservation-wall diag: surface batch exists to build a collision surface from");
      if (surfaceBatch != trackPtr->geometry.end()) {
        std::vector<CollisionTriangle> triangles;
        for (std::size_t v = 0; v + 2 < surfaceBatch->vertices.size(); v += 3) {
          CollisionTriangle tri;
          for (int k = 0; k < 3; k++) {
            tri.positions[k] = surfaceBatch->vertices[v + k].position;
            tri.normals[k] = surfaceBatch->vertices[v + k].normal;
          }
          triangles.push_back(tri);
        }
        auto trackWithCollision = std::make_shared<Track>(*loaded.track);
        trackWithCollision->collisionSurface = std::make_shared<TrackCollisionSurface>(std::move(triangles));
        const auto [finiteC, railHitsC, togglesC, finalSpeedC] = driveInto(trackWithCollision);
        check(finiteC, "reservation-wall diag (with collisionSurface): position/speed stays finite for 400 steps");
        check(railHitsC >= 1, "reservation-wall diag (with collisionSurface): driving into the taper still triggers a wall hit");
        check(togglesC <= 2,
              "reservation-wall diag (with collisionSurface): the render mesh's hole lines up with the analytical "
              "wall closely enough that the ship doesn't repeatedly toggle airborne near the gap edge (got " +
                  std::to_string(togglesC) + " toggles)");
        check(finalSpeedC > 100.0, "reservation-wall diag (with collisionSurface): speed recovers after the bounce");
      }
    }
  }

  {
    // CENTRAL_RESERVATION_PLAN.md M6: a Capped reservation must have a real, landable floor -- a
    // ship falling onto it lands (mirrors the existing "falls onto an external triangle" pattern
    // above) at the region's own elevation, not the main corridor's -- and its one-directional
    // rails must let the ship drive back off it afterward rather than trapping it there forever
    // (bidirectional rails, as every other MeshRegion still has, would).
    json input;
    input["version"] = 11;
    input["name"] = "m6-capped-landing-diag";
    json points = json::array();
    for (int i = 0; i < 8; i++) points.push_back({{"type", "position"}, {"pos", {0.0, 0.0, i * 200.0}}});
    json path = {{"id", "p0"}, {"closed", false}, {"points", points}};
    path["reservations"] = json::array({{{"id", "res1"}, {"t0", 0.45}, {"t1", 0.55}, {"width", 16.0}}});
    input["paths"] = json::array({path});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "M6 capped-landing diag track loads: " + loaded.error);
    if (loaded) {
      auto trackPtr = std::make_shared<Track>(*loaded.track);
      const auto region = std::find_if(trackPtr->meshRegions.begin(), trackPtr->meshRegions.end(),
                                       [](const MeshRegion& r) { return r.id.rfind("reservation-res1", 0) == 0; });
      check(region != trackPtr->meshRegions.end() && !region->polygons.empty(),
            "M6 capped-landing diag: the reservation region has a floor to land on");
      if (region != trackPtr->meshRegions.end() && !region->polygons.empty()) {
        Simulation sim(*trackPtr);
        // Straight down the middle of the void's footprint (centerline x=0), at the reservation's
        // own midpoint distance along z -- squarely over the floor, not near an edge.
        Ship ship = shipAt(sim, *trackPtr, Vec3(0.0, region->elevation + 5.0, 700.0), Vec3(0, 0, 1));
        ship.physics.airborne = true;
        ship.physics.verticalVel = -10.0;
        bool landed = false;
        for (int i = 0; i < 60 && !landed; i++) {
          ship.step(sim, 1.0 / 60.0, 0.0, 0.0, 0.0);
          landed = !ship.physics.airborne;
        }
        check(landed, "M6 capped-landing diag: the ship lands rather than falling through");
        check(landed && std::fabs(ship.physics.groundPos.y - region->elevation) < 0.5,
              "M6 capped-landing diag: it lands at the reservation's own (lower) floor elevation, not the road's");

        // Drive it sideways, off the floor and out through the boundary -- the one-directional
        // rail must not block this (only the reverse, track-into-void direction is blocked).
        ship.physics.forward.set(1, 0, 0);
        ship.physics.moveDir.set(1, 0, 0);
        ship.physics.speed = 40.0;
        const double startX = ship.physics.groundPos.x;
        bool finite = true;
        for (int i = 0; i < 90 && finite; i++) {
          ship.step(sim, 1.0 / 60.0, 1.0, 0.0, 0.0);
          finite = std::isfinite(ship.physics.groundPos.x) && std::isfinite(ship.physics.groundPos.z);
        }
        check(finite, "M6 capped-landing diag: position stays finite while driving off the floor");
        check(finite && ship.physics.groundPos.x > startX + 8.0,
              "M6 capped-landing diag: the ship actually crosses back out past the reservation's half-width, not "
              "trapped inside by its own rails");
      }
    }
  }

  {
    // CENTRAL_RESERVATION_PLAN.md M6 regression: every reservation rail's normal must point OUT of
    // the void. M6 made rails one-directional (slideAlongRails only blocks travel opposing a rail's
    // own normal), which turned normal orientation from cosmetic into load-bearing -- and the bake
    // was deriving each normal as a bare 90-degree rotation of the segment direction. Both flanks
    // are emitted in the same along-path direction, so that rotation landed outward on one flank
    // and inward on the other: every left-flank rail (and one of the two end caps) was silently
    // non-collidable, and a car could drive straight into the median from that side. It showed up
    // mainly at speed simply because that's when a car crosses the road far enough to reach it.
    json input;
    input["version"] = 11;
    input["name"] = "reservation-wall-orientation";
    json points = json::array();
    for (int i = 0; i < 8; i++) points.push_back({{"type", "position"}, {"pos", {0.0, 0.0, i * 200.0}}});
    json path = {{"id", "p0"}, {"closed", false}, {"points", points}};
    path["reservations"] = json::array({{{"id", "res1"}, {"t0", 0.45}, {"t1", 0.55}, {"width", 16.0}}});
    input["paths"] = json::array({path});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "reservation wall-orientation track loads: " + loaded.error);
    if (loaded) {
      auto trackPtr = std::make_shared<Track>(*loaded.track);
      Simulation sim(*trackPtr);
      const auto reg = std::find_if(trackPtr->meshRegions.begin(), trackPtr->meshRegions.end(),
                                    [](const MeshRegion& r) { return r.id.rfind("reservation-res1", 0) == 0; });
      check(reg != trackPtr->meshRegions.end() && !reg->rails.empty() && !reg->polygons.empty(),
            "wall-orientation: the Capped reservation region has rails and a floor polygon to test against");
      if (reg != trackPtr->meshRegions.end() && !reg->rails.empty() && !reg->polygons.empty()) {
        // Geometric, flank-agnostic orientation check: probing just off a rail's midpoint should
        // land inside the void on the -normal side and outside it on the +normal side. Rails right
        // at a taper tip are legitimately too thin to resolve either way, so only unambiguous ones
        // (exactly one probe inside) are graded -- there must be no inward-facing rail among them.
        int outward = 0, inward = 0, ambiguous = 0;
        for (const auto& rail : reg->rails) {
          const double mx = (rail.a.x + rail.b.x) * 0.5, mz = (rail.a.y + rail.b.y) * 0.5;
          const double probe = 0.25;
          const bool posInside = reg->contains(mx + rail.nx * probe, mz + rail.nz * probe);
          const bool negInside = reg->contains(mx - rail.nx * probe, mz - rail.nz * probe);
          if (posInside == negInside)
            ++ambiguous;
          else if (negInside)
            ++outward;
          else
            ++inward;
        }
        check(inward == 0, "wall-orientation: no reservation rail faces into the void (got " + std::to_string(inward) +
                               " inward of " + std::to_string(outward + inward) + " resolvable)");
        check(outward > reg->rails.size() / 2,
              "wall-orientation: most rails resolve unambiguously, so the check above has real coverage (" +
                  std::to_string(outward) + " outward, " + std::to_string(ambiguous) + " ambiguous)");

        // Narrow phase must block a boundary crossing from BOTH flanks, not just one.
        for (const double fromX : {9.0, -9.0}) {
          const double toX = -fromX * 0.2;
          Vec2d velocity{toX - fromX, 0.0};
          const MeshMoveResult moved = slideAlongRails(*reg, {fromX, 700.0}, {toX, 700.0}, velocity,
                                                       TrackCore::COLLISION_WALL_MARGIN, 0.0);
          check(moved.hit && !reg->contains(moved.x, moved.z),
                "wall-orientation: a crossing from x=" + std::to_string(static_cast<int>(fromX)) +
                    " is blocked and ends outside the void");
        }
      }

      // Full-physics sweep: drive at the median from both sides, at a range of approach angles and
      // at racing speed, and require the ship never enters the void's footprint. The shallow angles
      // matter most -- they track nearly parallel to the taper, which is how a car actually clips a
      // median at speed, and they were what first exposed the dead flank.
      if (reg != trackPtr->meshRegions.end() && !reg->polygons.empty()) {
        int breaches = 0;
        for (const double side : {1.0, -1.0}) {
          for (const double dirZ : {0.0, 1.0, 2.0, 4.0, 6.0, 8.0, 12.0, 20.0}) {
            for (const double speed : {40.0, 140.0}) {
              Ship ship = shipAt(sim, *trackPtr, Vec3(14.0 * side, 0.0, 500.0), Vec3(-side, 0, 0));
              Vec3 direction(-side, 0, dirZ);
              direction.normalize();
              ship.physics.moveDir.copy(direction);
              ship.physics.forward.copy(direction);
              ship.physics.speed = speed;
              for (int i = 0; i < 400; i++) {
                ship.step(sim, 1.0 / 60.0, 0.0, 0.0, 0.0);
                if (reg->contains(ship.physics.groundPos.x, ship.physics.groundPos.z)) ++breaches;
              }
            }
          }
        }
        check(breaches == 0,
              "wall-orientation: driving into the median from either side at any approach angle never "
              "breaches the void (got " +
                  std::to_string(breaches) + " frames inside)");
      }
    }
  }

  {
    // Reverse-gear regression: a car backing into a reservation wall must stay in reverse after
    // the bounce, not get flipped into forward gear (CENTRAL_RESERVATION_PLAN.md M2 bugfix). The
    // original bug: the collision response set `speed = hypot(vel)` (always non-negative) and
    // re-pointed moveDir to match, discarding which gear the car was in. Holding reverse the whole
    // time then decelerated the now-positive speed back down through zero and into reverse again
    // along the NEW (rotated) heading, driving the car straight back into the same wall from a
    // different angle -- an unbounded bounce-reverse-rebounce loop, worse near a reservation than
    // the track's outer edge since backing into a median is far more common than backing off-track.
    json input;
    input["version"] = 11;
    input["name"] = "diag-reverse";
    json points = json::array();
    for (int i = 0; i < 8; i++) points.push_back({{"type", "position"}, {"pos", {0.0, 0.0, i * 200.0}}});
    json path = {{"id", "p0"}, {"closed", false}, {"points", points}};
    path["reservations"] = json::array({{{"id", "res1"}, {"t0", 0.45}, {"t1", 0.55}, {"width", 16.0}}});
    input["paths"] = json::array({path});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "reverse-gear diag track loads: " + loaded.error);
    if (loaded) {
      Simulation sim(*loaded.track);
      Ship ship;
      const auto& centerline = loaded.track->paths[0].centerline;
      // Start near the end of the path (past the reservation, t~0.86), offset 3 units laterally
      // (inside a lane, not the gap), facing forward (+z) but driving in reverse the whole time --
      // moves backward (-z) through the reservation's tapered area.
      const Frame& startFrame = centerline[static_cast<std::size_t>(std::lround(0.86 * (centerline.size() - 1)))];
      Sample startSample;
      startSample.pos = startFrame.pos;
      startSample.tangent = startFrame.tangent;
      startSample.edgeRight = startFrame.edgeRight;
      startSample.normal = startFrame.normal;
      startSample.sLeft = startFrame.sLeft;
      startSample.sRight = startFrame.sRight;
      startSample.crossSectionCurvature = startFrame.crossSectionCurvature;
      startSample.crossSectionTightness = startFrame.crossSectionTightness;
      const SurfaceFrame startSurface = curvedSurfaceFrame(startSample, 3.0);
      sim.placeShipAtPose(ship, Pose{startSurface.pos, startSurface.normal, startFrame.tangent}, {});
      int railHits = 0;
      bool everWentPositiveAfterHit = false;
      double startZ = ship.physics.groundPos.z;
      bool finite = true;
      for (int i = 0; i < 1600 && finite; i++) {
        const StepResult step = ship.step(sim, 1.0 / 60.0, 0.0, 1.0, 0.0);
        if (step.railHit) {
          ++railHits;
          if (ship.physics.speed > 1.0) everWentPositiveAfterHit = true;
        }
        finite = std::isfinite(ship.physics.groundPos.x) && std::isfinite(ship.physics.groundPos.z) &&
                 std::isfinite(ship.physics.speed);
      }
      check(finite, "reverse-gear diag: position/speed stays finite for 1600 steps");
      check(railHits >= 1, "reverse-gear diag: backing past the reservation actually grazes the wall");
      check(!everWentPositiveAfterHit,
            "reverse-gear diag: a reverse-gear wall hit doesn't flip the car into forward gear");
      check(railHits <= 3, "reverse-gear diag: no repeated bounce-reverse-rebounce loop against the same wall (got " +
                               std::to_string(railHits) + " hits)");
      check(ship.physics.groundPos.z < startZ - 700.0,
            "reverse-gear diag: the car makes steady net progress backward through the reservation's area, not stuck "
            "oscillating in place");
    }
  }

  {
    // Reverse-gear regression on the corridor wall, where the road's *width* changes. Reversing
    // near the edge of a narrowing section, the shrinking hiS crosses under the car's lateral
    // offset with the car travelling dead straight -- `into` is exactly 0, so there is no impulse
    // and no bounce, yet the old code still rewrote speed/moveDir from the raw velocity: speed
    // flipped -33 to +32.34 and moveDir inverted 180 degrees. Held brake then decelerated that
    // positive speed back through zero while grip swung moveDir around to meet forward again,
    // slewing the car sideways to a near halt -- the reported "stutters and can get blocked at
    // certain place where the track width is not constant". Fix: only touch speed/moveDir when
    // into > 0, and preserve the gear sign when doing so.
    json input;
    input["version"] = 11;
    input["name"] = "diag-reverse-width";
    json points = json::array();
    for (int i = 0; i < 8; i++) points.push_back({{"type", "position"}, {"pos", {0.0, 0.0, i * 200.0}}});
    json path = {{"id", "p0"}, {"closed", false}, {"points", points}};
    // Wide at the far end, narrow in the middle: reversing from t~0.85 back toward the middle runs
    // the car along a converging edge for hundreds of metres.
    path["points"].push_back({{"type", "width"}, {"t", 0.0}, {"width", 16.0}});
    path["points"].push_back({{"type", "width"}, {"t", 0.45}, {"width", 16.0}});
    path["points"].push_back({{"type", "width"}, {"t", 0.75}, {"width", 46.0}});
    path["points"].push_back({{"type", "width"}, {"t", 1.0}, {"width", 46.0}});
    input["paths"] = json::array({path});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "reverse-width diag track loads: " + loaded.error);
    if (loaded) {
      Simulation sim(*loaded.track);
      Ship ship;
      const auto& centerline = loaded.track->paths[0].centerline;
      const Frame& startFrame = centerline[static_cast<std::size_t>(std::lround(0.85 * (centerline.size() - 1)))];
      Sample startSample;
      startSample.pos = startFrame.pos;
      startSample.tangent = startFrame.tangent;
      startSample.edgeRight = startFrame.edgeRight;
      startSample.normal = startFrame.normal;
      startSample.sLeft = startFrame.sLeft;
      startSample.sRight = startFrame.sRight;
      startSample.crossSectionCurvature = startFrame.crossSectionCurvature;
      startSample.crossSectionTightness = startFrame.crossSectionTightness;
      // Start inside the wide section's right edge -- comfortably within its wall margin there, but
      // outside the limit the narrow section further back will present.
      const SurfaceFrame startSurface = curvedSurfaceFrame(startSample, startFrame.sRight - 4.0);
      sim.placeShipAtPose(ship, Pose{startSurface.pos, startSurface.normal, startFrame.tangent}, {});
      Physics& p = ship.physics;
      const double startZ = p.groundPos.z;
      bool everWentPositive = false, finite = true, everSlow = false;
      for (int i = 0; i < 1200 && finite; i++) {
        ship.step(sim, 1.0 / 60.0, 0.0, 1.0, 0.0);
        // Once up to the reverse cap the car should stay pinned there; the wall may only ever slow
        // it via a genuine impulse, and there is none to be had reversing straight down a taper.
        if (i > 120) {
          if (p.speed > 0.0) everWentPositive = true;
          if (p.speed > -30.0) everSlow = true;
        }
        finite = std::isfinite(p.groundPos.x) && std::isfinite(p.groundPos.z) && std::isfinite(p.speed);
      }
      check(finite, "reverse-width diag: position/speed stays finite for 1200 steps");
      check(!everWentPositive,
            "reverse-width diag: a narrowing corridor wall doesn't flip the reversing car into forward gear");
      check(!everSlow,
            "reverse-width diag: brushing the narrowing wall doesn't bleed speed off a car that never "
            "actually drove into it");
      // 1200 steps at 60Hz is 20s; capped reverse (-33 m/s) covers 660m, and the car spends the
      // first ~2s getting up to that cap.
      check(p.groundPos.z < startZ - 640.0,
            "reverse-width diag: the car backs steadily through the narrowing section rather than juddering to a "
            "halt (travelled " +
                std::to_string(startZ - p.groundPos.z) + "m)");
    }
  }

  {
    // Wall-pinning regression, the *actual* cause of the reported "gets blocked where the track
    // width is not constant" (the gear fix above was necessary but not sufficient). When a ship is
    // laterally outside the corridor, `forceCurrentWall` deliberately keeps the sample taken at the
    // ship's OLD position instead of re-sampling at newPos. But the position is then rebuilt as
    // curvedSurfaceFrame(c, finalS) = c.pos + edgeRight*finalS + normal*lift, which has no
    // tangential term -- and `s`, the only channel newPos had into that expression, is clamped away
    // by finalS. So groundPos became a pure function of the OLD groundPos with velocity contributing
    // exactly nothing: a ship on the wall maps to itself and freezes there permanently, at full
    // indicated speed. A *narrowing* section is what makes it latch, because the shrinking hiS
    // re-clamps the ship every frame and keeps forceCurrentWall true. Modelled on the reported
    // track's own profile: a wide bulge (36 -> 157 -> 36) with the ship reversing out of it.
    json input;
    input["version"] = 11;
    input["name"] = "diag-wall-pinning";
    json points = json::array();
    const int positions = 12;
    for (int i = 0; i < positions; i++) {
      const double a = 2.0 * 3.14159265358979323846 * i / positions;
      points.push_back({{"type", "position"}, {"pos", {600.0 * std::cos(a), 0.0, 600.0 * std::sin(a)}}});
    }
    json path = {{"id", "p0"}, {"closed", true}, {"points", points}};
    path["points"].push_back({{"type", "width"}, {"t", 0.0}, {"width", 36.0}});
    path["points"].push_back({{"type", "width"}, {"t", 0.10}, {"width", 36.0}});
    path["points"].push_back({{"type", "width"}, {"t", 0.16}, {"width", 157.0}});
    path["points"].push_back({{"type", "width"}, {"t", 0.22}, {"width", 36.0}});
    path["points"].push_back({{"type", "width"}, {"t", 0.50}, {"width", 36.0}});
    input["paths"] = json::array({path});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "wall-pinning diag track loads: " + loaded.error);
    if (loaded) {
      Simulation sim(*loaded.track);
      const auto& centerline = loaded.track->paths[0].centerline;
      const Frame& startFrame = centerline[static_cast<std::size_t>(std::lround(0.16 * (centerline.size() - 1)))];
      Sample startSample;
      startSample.pos = startFrame.pos;
      startSample.tangent = startFrame.tangent;
      startSample.edgeRight = startFrame.edgeRight;
      startSample.normal = startFrame.normal;
      startSample.sLeft = startFrame.sLeft;
      startSample.sRight = startFrame.sRight;
      startSample.crossSectionCurvature = startFrame.crossSectionCurvature;
      startSample.crossSectionTightness = startFrame.crossSectionTightness;
      // Start deep inside the bulge, 75% of the way out to its edge: reversing (decreasing t) runs
      // the ship out of the 157-wide section into the 36-wide one, closing the wall onto it.
      const SurfaceFrame startSurface = curvedSurfaceFrame(startSample, startFrame.sRight * 0.75);
      Ship ship;
      sim.placeShipAtPose(ship, Pose{startSurface.pos, startSurface.normal, startFrame.tangent}, {});
      Physics& p = ship.physics;

      double travelled = 0.0;
      Vec3 previous = p.groundPos;
      int frozenFrames = 0, worstFrozenRun = 0;
      bool finite = true;
      for (int i = 0; i < 900 && finite; i++) {
        ship.step(sim, 1.0 / 60.0, 0.0, 1.0, 0.0);
        const double moved = std::hypot(p.groundPos.x - previous.x, p.groundPos.z - previous.z);
        travelled += moved;
        // "Frozen" = indicating real speed while not actually moving. That is the signature of the
        // bug; a legitimately stopped car has speed ~0 too.
        if (i > 60 && moved < 1e-9 && std::fabs(p.speed) > 1.0) {
          ++frozenFrames;
          worstFrozenRun = std::max(worstFrozenRun, frozenFrames);
        } else {
          frozenFrames = 0;
        }
        previous = p.groundPos;
        finite = std::isfinite(p.groundPos.x) && std::isfinite(p.groundPos.z) && std::isfinite(p.speed);
      }
      check(finite, "wall-pinning diag: position/speed stays finite for 900 steps");
      check(worstFrozenRun == 0,
            "wall-pinning diag: the ship never freezes in place while indicating speed (longest "
            "frozen run " +
                std::to_string(worstFrozenRun) + " frames)");
      // 900 frames at 60Hz is 15s; capped reverse (-33 m/s) covers 495m, less the ~2s spent
      // reaching the cap. Pinned, the old code managed ~145m of that.
      check(travelled > 450.0,
            "wall-pinning diag: the ship slides along the narrowing wall instead of locking onto "
            "it (travelled " +
                std::to_string(travelled) + "m of ~495m ideal)");
    }
  }

  {
    // The same pinning defect in forward gear, which CENTRAL_RESERVATION_PLAN.md section 4 had
    // logged separately as "an unsteered car driving straight through a sharp curve can grind to a
    // permanent halt against the outer corridor wall". Same root cause, same fix: an unsteered
    // full-throttle car runs out to the outer wall of a closed circle and must then keep sliding
    // along it, not stop dead. Before the fix this froze after ~150m.
    json input;
    input["version"] = 11;
    input["name"] = "diag-unsteered-curve";
    json points = json::array();
    const int positions = 12;
    for (int i = 0; i < positions; i++) {
      const double a = 2.0 * 3.14159265358979323846 * i / positions;
      points.push_back({{"type", "position"}, {"pos", {300.0 * std::cos(a), 0.0, 300.0 * std::sin(a)}}});
    }
    input["paths"] = json::array({{{"id", "p0"}, {"closed", true}, {"points", points}}});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "unsteered-curve diag track loads: " + loaded.error);
    if (loaded) {
      Simulation sim(*loaded.track);
      const Frame& startFrame = loaded.track->paths[0].centerline[0];
      Sample startSample;
      startSample.pos = startFrame.pos;
      startSample.tangent = startFrame.tangent;
      startSample.edgeRight = startFrame.edgeRight;
      startSample.normal = startFrame.normal;
      startSample.sLeft = startFrame.sLeft;
      startSample.sRight = startFrame.sRight;
      startSample.crossSectionCurvature = startFrame.crossSectionCurvature;
      startSample.crossSectionTightness = startFrame.crossSectionTightness;
      const SurfaceFrame startSurface = curvedSurfaceFrame(startSample, 0.0);
      Ship ship;
      sim.placeShipAtPose(ship, Pose{startSurface.pos, startSurface.normal, startFrame.tangent}, {});
      Physics& p = ship.physics;

      double travelled = 0.0, slowest = 1e9;
      Vec3 previous = p.groundPos;
      for (int i = 0; i < 1800; i++) {
        ship.step(sim, 1.0 / 60.0, 1.0, 0.0, 0.0);  // full throttle, zero steering
        travelled += std::hypot(p.groundPos.x - previous.x, p.groundPos.z - previous.z);
        previous = p.groundPos;
        if (i > 600) slowest = std::min(slowest, std::fabs(p.speed));
      }
      check(travelled > 1500.0,
            "unsteered-curve diag: an unsteered car keeps sliding along the outer wall rather "
            "than grinding to a halt (travelled " +
                std::to_string(travelled) + "m in 30s)");
      check(slowest > 20.0, "unsteered-curve diag: its speed doesn't bleed away against the wall (slowest " +
                                std::to_string(slowest) + " m/s)");
    }
  }

  if (failures) {
    std::cerr << failures << " track loader test(s) failed\n";
    return 1;
  }
  std::cout << "geometry oracle worst: " << worstOracleDelta << " (" << worstOracleRatio
            << "x gate, " << worstOracleField << ")\n";
  std::cout << "PASS: strict loader and " << found.size() << " reference-normalized mesh fixtures\n";
  return 0;
}
