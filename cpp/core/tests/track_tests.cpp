// track_tests.cpp — focused native track loading/bake/geometry/mesh tests.
// M2 validates strict current-schema loading and JS-produced normalization
// summaries. Later milestones add bake and physics assertions to this target.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "Simulation.hpp"
#include "Track.hpp"
#include "nlohmann/json.hpp"

using nlohmann::json;
using namespace tox;

namespace {

int failures = 0;

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
  check(std::isfinite(got) && std::fabs(got - want) <= tolerance,
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
      check(loaded.track->definition.version == 10, label + " uses current schema 10");
      check(!loaded.track->definition.paths.empty(), label + " has at least one path");
      check(!loaded.track->definition.meshAssets.empty(), label + " has mesh assets");
      check(!loaded.track->definition.meshes.empty(), label + " has mesh placements");

      const json actual = normalizedSummary(loaded.track->definition);
      check(expectedSummaries.contains(label), label + " has a JS normalization summary");
      if (expectedSummaries.contains(label))
        check(actual == expectedSummaries.at(label), label + " normalized summary matches JS\n  got:  " + dump(actual) +
                                                         "\n  want: " + dump(expectedSummaries.at(label)));

      check(expectedCompiled.contains(label), label + " has a JS compiled mesh summary");
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
        check(region.polygons.size() == expected["polygons"].size(), label + " polygon count matches JS");
        for (std::size_t p = 0; p < std::min(region.polygons.size(), expected["polygons"].size()); ++p) {
          check(region.polygons[p].polygonId == expected["polygons"][p]["polygonId"].get<int>(), label + " polygon keeps authored id");
          check(region.polygons[p].outer.size() == expected["polygons"][p]["outerCount"].get<std::size_t>(), label + " outer-loop topology matches JS");
          check(region.polygons[p].holes.size() == expected["polygons"][p]["holeCounts"].size(), label + " hole count matches JS");
          for (std::size_t h = 0; h < std::min(region.polygons[p].holes.size(), expected["polygons"][p]["holeCounts"].size()); ++h)
            check(region.polygons[p].holes[h].size() == expected["polygons"][p]["holeCounts"][h].get<std::size_t>(),
                  label + " hole-loop topology matches JS");
        }
        check(region.triangles.size() == expected["triangleCount"].get<std::size_t>(), label + " equivalent triangulation count matches JS");
        checkClose(triangleArea(region), expected["triangleArea"].get<double>(), 2e-9, label + " triangulated area");
        check(region.rails.size() == expected["rails"].size(), label + " rail count matches JS");
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
          check(surface->materialKey == "mesh-region" && !surface->hasUv && !surface->texture,
                label + " mesh surface keeps renderer-neutral material metadata");
        }
        if (rails != loaded.track->geometry.end()) {
          check(rails->vertices.size() == region.rails.size() * 6 && rails->indices.size() == region.rails.size() * 6,
                label + " rail geometry covers every compiled rail");
          check(rails->materialKey == "rail" && !rails->hasUv && !rails->texture,
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
  // polygon seams are not rails, and bounds padding follows the JS contract.
  const auto holeTrack = Track::fromFile(fixtureDir / "pad-with-hole.json");
  if (holeTrack && !holeTrack.track->meshRegions.empty()) {
    const MeshRegion& region = holeTrack.track->meshRegions[0];
    check(region.contains(-20, -20), "solid area of holed mesh contains world point");
    check(!region.contains(0, 0), "polygon hole excludes its world point");
    check(!region.contains(40, 0), "outside point is not contained");
    check(region.withinBounds(30.5, 0, 0.5) && !region.withinBounds(30.5, 0), "bounds query honors padding");
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

  // M3: independently baked curved/banked path against selected JS oracle data.
  const std::filesystem::path pathFixture = fixtureDir.parent_path() / "path" / "curved-banked.json";
  const json pathExpected = readJson(fixtureDir.parent_path() / "path" / "expected" / "curved-banked-summary.json");
  const TrackLoadResult pathLoaded = Track::fromFile(pathFixture);
  check(static_cast<bool>(pathLoaded), "curved/banked current-schema path loads and bakes: " + pathLoaded.error);
  if (pathLoaded) {
    const Track& track = *pathLoaded.track;
    check(track.paths.size() == 1, "native bake produces one path");
    const Path& path = track.paths[0];
    const json& expectedPath = pathExpected["paths"][0];
    check(path.centerline.size() == expectedPath["frameCount"].get<std::size_t>(), "adaptive physics frame count matches JS");
    check(path.anchors.size() == expectedPath["anchors"].size(), "anchor count matches JS");
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
            "adaptive render triangle count matches JS: " + id);
      check(foundBatch->hasUv == expectedBatch["hasUv"].get<bool>(), "render UV presence matches JS: " + id);
      if (expectedBatch.contains("texture") && !expectedBatch["texture"].is_null()) {
        check(foundBatch->texture.has_value(), "render texture metadata exists: " + id);
        if (foundBatch->texture) {
          check(foundBatch->texture->assetId == expectedBatch["texture"]["assetId"].get<std::string>() &&
                    foundBatch->texture->tile == expectedBatch["texture"]["tile"].get<int>(),
                "render texture metadata matches JS: " + id);
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

  if (failures) {
    std::cerr << failures << " track loader test(s) failed\n";
    return 1;
  }
  std::cout << "PASS: strict loader and " << found.size() << " JS-normalized mesh fixtures\n";
  return 0;
}
