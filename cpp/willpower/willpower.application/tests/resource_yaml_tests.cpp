#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "willpower/application/resourcesystem/DirectoryResourceLocation.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: resource_yaml_tests <Resources.yaml>\n";
    return 2;
  }

  try {
    const std::filesystem::path definition = std::filesystem::absolute(argv[1]);
    wp::application::resourcesystem::DirectoryResourceLocation location(
        nullptr, definition.parent_path().string(), definition.filename().string());
    location.scan();

    const auto& namespaces = location.getNamespaceRecords();
    const auto root = namespaces.find("");
    const auto tracks = namespaces.find("Tracks");
    if (root == namespaces.end() || tracks == namespaces.end())
      throw std::runtime_error("YAML resource namespaces were not reconstructed.");
    if (!root->second.resourceRecords.contains("TungstenMonoxide"))
      throw std::runtime_error("Root Game resource was not reconstructed.");
    if (!tracks->second.resourceRecords.contains("NewTrack"))
      throw std::runtime_error("Tracks/NewTrack resource was not reconstructed.");

    const auto& track = tracks->second.resourceRecords.at("NewTrack");
    if (track.definitions.empty() || track.dependentResources.size() != 7)
      throw std::runtime_error("Track definitions or dependencies were not reconstructed.");

    std::cout << "Willpower YAML resource definitions passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
