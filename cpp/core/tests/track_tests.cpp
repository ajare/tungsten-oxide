// track_tests.cpp — focused native track loading/geometry/mesh tests.
//
// Milestone M0 only establishes the shared current-schema fixture harness. The
// loader, bake, Willpower adapter, and simulation scenarios are added here one
// milestone at a time by MESH_CPP_PORT_PLAN.md.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include "nlohmann/json.hpp"

using nlohmann::json;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: track_tests <mesh-fixture-directory>\n";
    return 2;
  }

  const std::filesystem::path fixtureDir = argv[1];
  check(std::filesystem::is_directory(fixtureDir), "fixture directory exists");

  const std::set<std::string> expected{
      "concave-railed-pad.json", "corridor-mesh-bridge.json", "mesh-effects.json",
      "overlapping-elevations.json", "pad-with-hole.json", "shared-seam.json",
      "transformed-square.json"};
  std::set<std::string> found;

  if (std::filesystem::is_directory(fixtureDir)) {
    for (const auto& entry : std::filesystem::directory_iterator(fixtureDir)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      found.insert(entry.path().filename().string());

      std::ifstream input(entry.path());
      check(input.good(), entry.path().string() + " opens");
      if (!input) continue;

      try {
        json track;
        input >> track;
        const std::string label = entry.path().filename().string();
        check(track.value("version", -1) == 10, label + " uses current schema 10");
        check(track.contains("paths") && track["paths"].is_array() && !track["paths"].empty(),
              label + " has at least one path");
        check(track.contains("meshAssets") && track["meshAssets"].is_object() && !track["meshAssets"].empty(),
              label + " has mesh assets");
        check(track.contains("meshes") && track["meshes"].is_array() && !track["meshes"].empty(),
              label + " has mesh placements");
      } catch (const std::exception& error) {
        check(false, entry.path().string() + " parses: " + error.what());
      }
    }
  }

  check(found == expected, "fixture inventory matches the seven shared M0 cases");
  if (failures) {
    std::cerr << failures << " track fixture test(s) failed\n";
    return 1;
  }
  std::cout << "PASS: " << found.size() << " current-schema mesh fixtures\n";
  return 0;
}
