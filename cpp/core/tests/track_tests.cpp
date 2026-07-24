// track_tests.cpp — focused native track loading/bake/geometry/mesh tests.
// M2 validates strict current-schema loading and JS-produced normalization
// summaries. Later milestones add bake and physics assertions to this target.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

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
    }
  }

  check(found == expectedFiles, "fixture inventory matches the seven shared M0 cases");

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
      check(definition.triggers.empty(), "automatic Finish creation is deferred to M3 path baking");
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
