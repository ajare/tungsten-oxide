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

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: track_tests <fixtures-directory>\n";
    return 2;
  }
  // The fixtures ROOT directory (parent of path/, formerly also mesh/ -- Mesh regions and their
  // fixtures were removed, DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2), not a specific fixture-kind
  // subdirectory.
  const std::filesystem::path fixtureDir = argv[1];
  check(std::filesystem::is_directory(fixtureDir), "fixtures directory exists");

  // M3: independently baked curved/banked path against selected reference fixture data.
  const std::filesystem::path pathFixture = fixtureDir / "path" / "curved-banked.json";
  const json pathExpected = readJson(fixtureDir / "path" / "expected" / "curved-banked-summary.json");
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
          const double lateral = glm::dot(vertex.position - trigger.center, trigger.right);
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
    simulation.setMeshPhysicsEnabled(false);
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
    gridSim.setMeshPhysicsEnabled(false);
    const std::vector<Pose> poses = StartGrid::startingGridPoses(gridSim, track, 8);
    check(poses.size() == 8, "startingGridPoses produces the requested roster size");
    bool allFinite = true, allUnit = true;
    for (const Pose& pose : poses) {
      if (!std::isfinite(pose.pos.x) || !std::isfinite(pose.pos.y) || !std::isfinite(pose.pos.z)) allFinite = false;
      if (std::fabs(glm::length(pose.up) - 1.0) > 1e-9 || std::fabs(glm::length(pose.forward) - 1.0) > 1e-9) allUnit = false;
    }
    check(allFinite, "starting grid poses are all finite");
    check(allUnit, "starting grid pose forward/up are unit vectors");
    check(glm::distance(poses[0].pos, poses[1].pos) > 0.1, "front row grid slots are laterally offset");

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
    session.setMeshPhysicsEnabled(false);
    check(session.ships().size() == 1, "GameSession builds the requested roster size");
    if (!session.ships().empty()) {
      check(session.ships()[0].race.finishId == "curve-finish" && session.ships()[0].race.intermediateIds.empty(),
            "GameSession roster derives race state from track triggers");
    }

    const Vec3 startPos = session.ships()[0].physics.groundPos;
    std::vector<ControlIntent> idle(1);
    session.step(idle, 1.0 / 60.0);
    check(glm::distance(session.ships()[0].physics.groundPos, startPos) < 1.0,
          "an idle intent leaves a parked ship close to its starting-grid pose");
    check(session.sessionTime() > 0.0, "GameSession accumulates a deterministic session clock");

    // Drive forward long enough to leave the starting-grid pose...
    std::vector<ControlIntent> drive(1);
    drive[0].throttle = 1.0;
    for (int frame = 0; frame < 30; frame++) session.step(drive, 1.0 / 60.0);
    check(glm::distance(session.ships()[0].physics.groundPos, startPos) > 1.0,
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
    check(glm::distance(session.ships()[0].physics.groundPos, startPos) < 1.0,
          "an explicit respawn with no checkpoint reached returns to the starting-grid pose");
  }

  if (pathLoaded) {
    // Ship::step's meshModeOverride: lets a single call pick a mode regardless of what
    // Simulation::meshPhysicsEnabled() says. GameSession::stepGhost (tested below) is built on
    // this; test it directly first since it's the simpler surface.
    auto overrideTrack = std::make_shared<Track>(*pathLoaded.track);
    CollisionTriangle flatRoad;
    flatRoad.positions[0] = Vec3(-1000, 4, -1000);
    flatRoad.positions[1] = Vec3(1000, 4, -1000);
    flatRoad.positions[2] = Vec3(0, 4, 1000);
    flatRoad.normals[0] = flatRoad.normals[1] = flatRoad.normals[2] = UP;
    overrideTrack->collisionSurface = std::make_shared<TrackCollisionSurface>(std::vector<CollisionTriangle>{flatRoad});
    Simulation overrideSim(*overrideTrack);
    // Pinned false (despite mesh physics now being the default) so the assertion below actually
    // exercises the override forcing mesh mode against an ambient flag that disagrees with it.
    overrideSim.setMeshPhysicsEnabled(false);

    Ship forcedMesh = shipAt(overrideSim, *overrideTrack, {0, 4, 0});
    forcedMesh.physics.speed = 20;
    const StepResult meshResult = forcedMesh.step(overrideSim, 1.0 / 60.0, 1, 0, 0, true);
    check(std::fabs(meshResult.surfaceNormal.y - 1.0) < 1e-9,
          "meshModeOverride=true uses mesh physics even though meshPhysicsEnabled() is false");

    // Same starting state, opposite override, with meshPhysicsEnabled() itself now flipped to
    // true too -- so both results below come from the override alone, not from the ambient flag.
    overrideSim.setMeshPhysicsEnabled(true);
    Ship forcedMesh2 = shipAt(overrideSim, *overrideTrack, {0, 4, 0});
    forcedMesh2.physics.speed = 20;
    forcedMesh2.step(overrideSim, 1.0 / 60.0, 1, 0, 0, true);
    Ship forcedAnalytic = shipAt(overrideSim, *overrideTrack, {0, 4, 0});
    forcedAnalytic.physics.speed = 20;
    forcedAnalytic.step(overrideSim, 1.0 / 60.0, 1, 0, 0, false);
    check(glm::distance(forcedMesh2.physics.groundPos, forcedAnalytic.physics.groundPos) > 1e-6,
          "meshModeOverride selects the mode itself, independent of meshPhysicsEnabled()");
  }

  if (pathLoaded) {
    // GameSession::stepGhost: the debug "other physics method" ghost projection. Real ships run
    // mesh mode (matching the flat road below); the ghost is driven with meshModeOverride's
    // opposite, i.e. analytic.
    auto ghostTrack = std::make_shared<Track>(*pathLoaded.track);
    GameSession probeSession(ghostTrack, 1);
    const double spawnY = probeSession.ships()[0].physics.groundPos.y;
    CollisionTriangle flatRoad;
    flatRoad.positions[0] = Vec3(-1000, spawnY, -1000);
    flatRoad.positions[1] = Vec3(1000, spawnY, -1000);
    flatRoad.positions[2] = Vec3(0, spawnY, 1000);
    flatRoad.normals[0] = flatRoad.normals[1] = flatRoad.normals[2] = UP;
    ghostTrack->collisionSurface = std::make_shared<TrackCollisionSurface>(std::vector<CollisionTriangle>{flatRoad});

    GameSession ghostSession(ghostTrack, 1);
    ghostSession.setMeshPhysicsEnabled(true);
    Ship ghost = ghostSession.ships()[0];
    const Ship realShipBefore = ghostSession.ships()[0];

    std::vector<ControlIntent> driveIntent(1);
    driveIntent[0].throttle = 1.0;
    driveIntent[0].steer = 0.3;
    bool everDiverged = false;
    bool finiteThroughout = true;
    for (int frame = 0; frame < 90 && finiteThroughout; ++frame) {
      ghostSession.step(driveIntent, 1.0 / 60.0);
      ghostSession.stepGhost(ghost, driveIntent[0], 1.0 / 60.0);
      finiteThroughout = std::isfinite(ghost.physics.groundPos.x) && std::isfinite(ghost.physics.groundPos.y) &&
                         std::isfinite(ghost.physics.groundPos.z);
      if (glm::distance(ghost.physics.groundPos, ghostSession.ships()[0].physics.groundPos) > 0.5)
        everDiverged = true;
    }
    check(finiteThroughout, "stepGhost's projected position stays finite while driving");
    check(everDiverged,
          "stepGhost visibly diverges from the real ship when driven with the opposite physics mode");

    // Safety: stepGhost must not perturb the real roster or its event stream. `ghost` isn't a
    // roster member -- if GameSession's onTriggerFired ship-index pointer arithmetic (`&ship -
    // ships_.data()`) ever ran against it, this would be undefined behavior, not just a wrong
    // answer, so this checks the callback-suppression itself, not merely its visible effect.
    check(ghostSession.ships().size() == 1 &&
              glm::distance(ghostSession.ships()[0].physics.groundPos, realShipBefore.physics.groundPos) > 0.01,
          "stepGhost does not touch the real roster (it moved only via its own ghostSession.step calls)");
    const std::size_t eventsAfterGhost = ghostSession.events().size();
    ghostSession.stepGhost(ghost, driveIntent[0], 1.0 / 60.0);
    check(ghostSession.events().size() == eventsAfterGhost,
          "stepGhost never appends to GameSession::events()");

    // Respawn path: must reset the ghost via Ship::respawn without touching the real session.
    const Vec3 ghostPosBeforeRespawn = ghost.physics.groundPos;
    const Vec3 startGridPos = ghostSession.ships()[0].startPose.pos;
    ControlIntent respawnGhost;
    respawnGhost.respawn = true;
    ghostSession.stepGhost(ghost, respawnGhost, 1.0 / 60.0);
    check(glm::distance(ghost.physics.groundPos, startGridPos) < 1.0 &&
              glm::distance(ghost.physics.groundPos, ghostPosBeforeRespawn) > 0.01,
          "stepGhost's respawn intent resets the ghost to its starting-grid pose");
  }

  // A minimal open path (4 positions + default width/roll/crossSection aux points, matching what
  // the loader itself injects for a schema-current file) used as a template below -- inline rather
  // than a fixture file, now that the fixture this used to be (transformed-square.json, a Mesh
  // region fixture) is gone (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2).
  json basePoints = json::array();
  basePoints.push_back({{"type", "position"}, {"id", "p0"}, {"pos", {0, 0, -120}}, {"weight", 1}});
  basePoints.push_back({{"type", "position"}, {"id", "p1"}, {"pos", {0, 0, -40}}, {"weight", 1}});
  basePoints.push_back({{"type", "position"}, {"id", "p2"}, {"pos", {0, 0, 40}}, {"weight", 1}});
  basePoints.push_back({{"type", "position"}, {"id", "p3"}, {"pos", {0, 0, 120}}, {"weight", 1}});
  basePoints.push_back({{"type", "width"}, {"t", 0}, {"width", 24}});
  basePoints.push_back({{"type", "width"}, {"t", 1}, {"width", 24}});
  basePoints.push_back({{"type", "roll"}, {"t", 0}, {"roll", 0}});
  basePoints.push_back({{"type", "roll"}, {"t", 1}, {"roll", 0}});
  basePoints.push_back({{"type", "crossSection"}, {"t", 0}, {"curvature", 0}, {"tightness", 1}, {"thickness", 4}});
  basePoints.push_back({{"type", "crossSection"}, {"t", 1}, {"curvature", 0}, {"tightness", 1}, {"thickness", 4}});
  json basePath = {{"id", "path-main"}, {"closed", false}, {"points", std::move(basePoints)}};
  json baseTrigger = {{"id", "checkpoint-finish"},
                      {"type", "checkpoint"},
                      {"role", "finish"},
                      {"host", {{"kind", "path"}, {"pathId", "path-main"}, {"t", 0.0487653}}},
                      {"width", 24},
                      {"height", 12},
                      {"rotation", 0},
                      {"direction", "forward"}};
  json base = {{"version", TrackCore::TRACK_SCHEMA_VERSION},
              {"name", "Fixture - base"},
              {"start", {{"path", 0}, {"point", 0}, {"reverse", false}}},
              {"handling", {{"maxSpeed", 140}, {"accel", 71}, {"turnSpeed", 137.5}, {"weight", 1000}}},
              {"zones", json::array()},
              {"triggers", json::array({std::move(baseTrigger)})},
              {"disjointSeams", json::array()},
              {"junctions", json::array()},
              {"selfIntersectionOverrides", json::array()},
              {"textureAssets", json::object()},
              {"paths", json::array({std::move(basePath)})}};

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
    // A zone/trigger hosted on a drivable mesh object placement (DRIVABLE_MESH_OBJECTS_PLAN.md
    // Milestone 3.5): resolves purely from the placement's own 6-DOF transform, no `.mppmodel`
    // involved (core never loads one -- see the plan's "`.mppmodel` loading is host-only"
    // architecture note).
    json input = base;
    input["meshObjects"] = json::array({json{{"id", "platform-1"},
                                             {"modelId", "ramp.mppmodel"},
                                             {"x", 100.0},
                                             {"y", 5.0},
                                             {"z", 200.0},
                                             {"yaw", 90.0},
                                             {"pitch", 0.0},
                                             {"roll", 0.0},
                                             {"scaleX", 1.0},
                                             {"scaleY", 1.0},
                                             {"scaleZ", 1.0}}});
    input["zones"].push_back({{"id", "platform-boost"},
                              {"effect", "velocityChange"},
                              {"width", 12},
                              {"length", 20},
                              {"factor", 1.5},
                              {"duration", 2.0},
                              {"host", {{"kind", "meshObject"}, {"meshObjectId", "platform-1"}, {"localPosition", {{"x", 3.0}, {"y", 0.0}, {"z", 0.0}}}, {"localYaw", 15.0}}}});
    input["triggers"].push_back({{"id", "platform-checkpoint"},
                                 {"type", "checkpoint"},
                                 {"role", "intermediate"},
                                 {"width", 10},
                                 {"height", 8},
                                 {"rotation", 0},
                                 {"direction", "both"},
                                 {"host", {{"kind", "meshObject"}, {"meshObjectId", "platform-1"}, {"localPosition", {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}}}}});
    const auto loaded = Track::fromJson(input.dump());
    check(static_cast<bool>(loaded), "a track with a drivable mesh object placement loads: " + loaded.error);
    if (loaded) {
      const auto& definition = loaded.track->definition;
      check(definition.meshObjects.size() == 1 && definition.meshObjects[0].id == "platform-1", "the placement round-trips");
      const auto zoneIt = std::find_if(loaded.track->zones.begin(), loaded.track->zones.end(),
                                       [](const Zone& z) { return z.id == "platform-boost"; });
      check(zoneIt != loaded.track->zones.end() && zoneIt->kind == "meshObject", "the meshObject-hosted zone compiles");
      if (zoneIt != loaded.track->zones.end()) {
        // Placement at (100, 5, 200), local offset (3, 0, 0): a rotation about Y preserves length,
        // so the resolved world position must sit exactly `localPosition`'s magnitude (3) from the
        // placement's own position, in the XZ plane -- true regardless of the yaw's exact sign
        // convention (deliberately not asserting a specific x/z split here, only this invariant).
        const double dx = zoneIt->x - 100.0, dz = zoneIt->z - 200.0;
        checkClose(std::sqrt(dx * dx + dz * dz), 3.0, 1e-9, "meshObject-hosted zone world position is 3m from the placement, matching localPosition's length");
      }
      const auto triggerIt = std::find_if(loaded.track->triggers.begin(), loaded.track->triggers.end(),
                                          [](const Trigger& t) { return t.id == "platform-checkpoint"; });
      check(triggerIt != loaded.track->triggers.end(), "the meshObject-hosted trigger compiles");
      if (triggerIt != loaded.track->triggers.end()) {
        checkClose(triggerIt->center.x, 100.0, 1e-9, "meshObject-hosted trigger world center.x resolves from the placement transform");
        checkClose(triggerIt->center.z, 200.0, 1e-9, "meshObject-hosted trigger world center.z resolves from the placement transform");
        check(std::isfinite(triggerIt->right.x) && std::fabs(glm::length(triggerIt->right) - 1.0) < 1e-9,
              "meshObject-hosted trigger gets a unit-length right axis");
      }
    }
  }
  {
    // Same scenario as the inline check above, but as a committed fixture file
    // (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3.6) -- exercises Track::fromFile's file-I/O path,
    // not just fromJson's string path, and gives Milestone 6's eventual headless tool (and anyone
    // else) a real on-disk example of the schema to point at.
    const auto loaded = Track::fromFile(fixtureDir / "mesh-object" / "basic-placement.json");
    check(static_cast<bool>(loaded), "the mesh-object fixture loads: " + loaded.error);
    if (loaded) {
      check(loaded.track->definition.meshObjects.size() == 1, "the fixture's placement round-trips");
      check(std::any_of(loaded.track->zones.begin(), loaded.track->zones.end(), [](const Zone& z) { return z.kind == "meshObject"; }),
            "the fixture's meshObject-hosted zone compiles");
      check(std::any_of(loaded.track->triggers.begin(), loaded.track->triggers.end(), [](const Trigger& t) { return t.id == "platform-checkpoint"; }),
            "the fixture's meshObject-hosted trigger compiles");
    }
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
    smooth.normals[1] = glm::normalize(Vec3(0, 1, 1));
    smooth.normals[2] = glm::normalize(Vec3(1, 1, 0));
    TrackCollisionSurface smoothSurface({smooth});
    auto smoothHit = smoothSurface.nearestAlongAxis({0, 0.1, 0}, {0, 1, 0}, 1);
    check(smoothHit && smoothHit->normal.y < 1.0 && smoothHit->normal.y > 0.7,
          "contact normal barycentrically interpolates exported vertex normals");

    // A wall facing back toward the probe origin (normal opposes the probe axis) -- e.g. the inner
    // face of a track-side wall, as seen by a ship driving inside the track. nearestAlongAxis's
    // road-facing filter can never accept this; nearestAcrossAxis has no such filter and is what a
    // lateral/wall probe needs instead.
    CollisionTriangle wall;
    wall.positions[0] = {3, -1, -2};
    wall.positions[1] = {3, -1, 2};
    wall.positions[2] = {3, 1, 0};
    wall.normals[0] = wall.normals[1] = wall.normals[2] = {-1, 0, 0};
    wall.surfaceId = 30;
    TrackCollisionSurface wallSurface({wall});
    check(!wallSurface.nearestAlongAxis({0, 0, 0}, {1, 0, 0}, 5).has_value(),
          "nearestAlongAxis rejects a wall whose normal opposes the probe axis");
    auto wallHit = wallSurface.nearestAcrossAxis({0, 0, 0}, {1, 0, 0}, 5);
    check(wallHit && wallHit->surfaceId == 30 && std::fabs(wallHit->position.x - 3) < 1e-9,
          "nearestAcrossAxis finds the same wall regardless of which way its normal faces");
    check(!wallSurface.nearestAcrossAxis({0, 0, 0}, {0, 1, 0}, 5).has_value(),
          "nearestAcrossAxis still finds nothing along an axis that misses the geometry entirely");
  }

  {
    // sweepWall() had no dedicated coverage at all before DRIVABLE_MESH_OBJECTS_PLAN.md Milestone
    // 6.2 -- every existing BVH test above exercises nearestAlongAxis/nearestAcrossAxis/sweep, not
    // the TwoSidedWall filter stepMeshPhysics's lateral wall-bounce logic actually depends on. A
    // tunnel cross-section (floor + ceiling, both excluded by the |dot(normal,UP)|>0.5 floor/
    // ceiling filter, plus two opposing walls) is exactly the genuinely-3D, multiple-BVH-leaf
    // shape Milestone 6.1's own validation asset uses, so this mirrors that asset in miniature.
    auto quad = [](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal, int surfaceId) {
      CollisionTriangle first, second;
      first.positions[0] = a;
      first.positions[1] = b;
      first.positions[2] = c;
      second.positions[0] = a;
      second.positions[1] = c;
      second.positions[2] = d;
      for (int corner = 0; corner < 3; ++corner) first.normals[corner] = second.normals[corner] = normal;
      first.surfaceId = second.surfaceId = surfaceId;
      return std::vector<CollisionTriangle>{first, second};
    };
    std::vector<CollisionTriangle> tunnel;
    auto append = [&](std::vector<CollisionTriangle> quadTris) {
      tunnel.insert(tunnel.end(), quadTris.begin(), quadTris.end());
    };
    append(quad({-5, 0, 5}, {5, 0, 5}, {5, 0, 15}, {-5, 0, 15}, {0, 1, 0}, 1));     // floor
    append(quad({-5, 6, 5}, {5, 6, 5}, {5, 6, 15}, {-5, 6, 15}, {0, -1, 0}, 2));    // ceiling
    append(quad({-4, 0, 5}, {-4, 6, 5}, {-4, 6, 15}, {-4, 0, 15}, {1, 0, 0}, 3));   // left wall
    append(quad({4, 0, 5}, {4, 6, 5}, {4, 6, 15}, {4, 0, 15}, {-1, 0, 0}, 4));      // right wall
    TrackCollisionSurface tunnelSurface(tunnel);

    auto leftHit = tunnelSurface.sweepWall({0, 1, 10}, {-10, 1, 10});
    check(leftHit && leftHit->surfaceId == 3 && std::fabs(leftHit->position.x + 4) < 1e-9,
          "sweepWall finds the left wall of a tunnel cross-section");
    check(leftHit && glm::dot(leftHit->normal, Vec3(1, 0, 10) - leftHit->position) > 0,
          "sweepWall orients the contact normal back toward the side the sweep started from");

    check(!tunnelSurface.sweepWall({0, 1, 10}, {0, 10, 10}).has_value(),
          "sweepWall never reports the floor/ceiling of a tunnel as a wall, even when a segment "
          "would otherwise cross it");

    auto crossingHit = tunnelSurface.sweepWall({-10, 1, 10}, {10, 1, 10});
    check(crossingHit && crossingHit->surfaceId == 3 && std::fabs(crossingHit->position.x + 4) < 1e-9,
          "sweepWall picks the nearer of two walls a segment crosses (left, not right)");
  }

  {
    auto loaded = Track::fromJson(base.dump());
    check(static_cast<bool>(loaded), "external-contact ship fixture loads");
    if (loaded) {
      Track& track = *loaded.track;
      Simulation analytical(track);
      analytical.setMeshPhysicsEnabled(false);
      const Pose start = StartGrid::startingGridPoses(analytical, track, 1).front();
      Vec3 right = normalizeSafe(glm::cross(start.up, start.forward));
      const Vec3 center = start.pos + start.up * 2.0;
      const Vec3 a = center + right * -20.0 + start.forward * -20.0;
      const Vec3 b = center + right * 20.0 + start.forward * -20.0;
      const Vec3 c = center + right * 20.0 + start.forward * 20.0;
      const Vec3 d = center + right * -20.0 + start.forward * 20.0;
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
      // Tests the analytic pipeline's late BVH-authoritative-contact override specifically (see
      // docs/core.md's "Final collision-surface pass"), not full mesh mode -- pin explicitly.
      external.setMeshPhysicsEnabled(false);

      Ship parked = shipAt(external, track, start.pos, start.forward);
      const StepResult parkedStep = parked.step(external, 1.0 / 120.0, 0, 0, 0);
      check(std::fabs(glm::dot(parked.physics.groundPos - center, start.up)) < 1e-9 &&
                !parked.physics.airborne && glm::dot(parkedStep.surfaceNormal, start.up) > 0.999999,
            "Ship::step makes an external triangle surface authoritative for parked contact");

      Ship falling = shipAt(external, track, center + start.up * 2.0, start.forward);
      falling.physics.airborne = true;
      falling.physics.verticalVel = -10.0;
      falling.prevTriggerPos = falling.physics.groundPos;
      falling.step(external, 0.15, 0, 0, 0);
      check(!falling.physics.airborne &&
                std::fabs(glm::dot(falling.physics.groundPos - center, start.up)) < 1e-9,
            "Ship::step lands on an external triangle swept before the analytical road");
    }
  }

  // Hard-break check (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2.5): a track still carrying
  // meshAssets/meshes (removed in schema 12) must fail to load with an explicit, actionable
  // error rather than silently parsing as if the meshes had been deleted. Reuses one of the
  // now-orphaned mesh/ fixtures (DRIVABLE_MESH_OBJECTS_PLAN.md 2.6 leaves that directory in place
  // as a fixed pre-removal input for exactly this check) rather than hand-authoring a new one.
  {
    const TrackLoadResult meshFixtureLoaded = Track::fromFile(fixtureDir / "mesh" / "concave-railed-pad.json");
    check(!static_cast<bool>(meshFixtureLoaded), "a track with meshAssets/meshes fails to load");
    check(meshFixtureLoaded.error.find("Mesh regions") != std::string::npos, "the hard-break error names Mesh regions: " + meshFixtureLoaded.error);
  }

  if (failures) {
    std::cerr << failures << " track loader test(s) failed\n";
    return 1;
  }
  std::cout << "geometry oracle worst: " << worstOracleDelta << " (" << worstOracleRatio
            << "x gate, " << worstOracleField << ")\n";
  std::cout << "PASS: strict loader and path/reservation/physics fixtures\n";
  return 0;
}
