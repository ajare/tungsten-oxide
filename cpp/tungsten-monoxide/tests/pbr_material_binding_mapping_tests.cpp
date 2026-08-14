// Material-binding migration guard. This reads only .mppmodel metadata and Resources.yaml; it does
// not create an MPP renderer or alter runtime resources. Embedded material keys must retain their
// exact DependentResource IDs while resolving through stable logical PBR bindings.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace {

struct DirectoryEntry {
  std::uint32_t type{0};
  std::uint32_t start{0};
  std::uint32_t end{0};
  std::uint32_t count{0};
};

struct ResourceDeclaration {
  std::string type;
  std::string binding;
  std::map<std::string, std::string> dependents;
};

using ResourceDeclarations = std::map<std::string, ResourceDeclaration>;

std::uint16_t readU16(std::ifstream& input) {
  std::uint16_t value = 0;
  input.read(reinterpret_cast<char*>(&value), sizeof(value));
  if (!input) throw std::runtime_error("Unexpected end of .mppmodel file.");
  return value;
}

std::uint32_t readU32(std::ifstream& input) {
  std::uint32_t value = 0;
  input.read(reinterpret_cast<char*>(&value), sizeof(value));
  if (!input) throw std::runtime_error("Unexpected end of .mppmodel file.");
  return value;
}

std::string readString(std::ifstream& input) {
  const std::uint32_t size = readU32(input);
  std::string result(size, '\0');
  if (size != 0) input.read(result.data(), size);
  if (!input) throw std::runtime_error("Unexpected end of .mppmodel string.");
  return result;
}

std::set<std::string> readMeshMaterialKeys(const std::filesystem::path& modelPath) {
  std::ifstream input(modelPath, std::ios::binary);
  if (!input) throw std::runtime_error("Could not open model '" + modelPath.string() + "'.");

  char magic[4]{};
  input.read(magic, sizeof(magic));
  if (!input || std::string(magic, sizeof(magic)) != "MPPM")
    throw std::runtime_error("Model '" + modelPath.string() + "' has invalid MPPM magic.");

  (void)readU16(input);
  (void)readU16(input);
  (void)readU32(input);

  std::vector<DirectoryEntry> directory(6);
  for (DirectoryEntry& entry : directory) {
    entry.type = readU32(input);
    entry.start = readU32(input);
    entry.end = readU32(input);
    entry.count = readU32(input);
  }

  // ModelSerializer's directory type 5 is MeshMetadata. Select by the serialized type rather
  // than relying on directory order so a malformed/reordered directory fails clearly.
  const DirectoryEntry* meshes = nullptr;
  for (const DirectoryEntry& entry : directory)
    if (entry.type == 5) meshes = &entry;
  if (meshes == nullptr) throw std::runtime_error("Model has no MeshMetadata directory entry.");

  input.seekg(meshes->start);
  std::set<std::string> materialKeys;
  for (std::uint32_t i = 0; i < meshes->count; ++i) {
    const std::string meshName = readString(input);
    (void)readU32(input);  // primitive type
    (void)readU32(input);  // primitive count
    const std::string material = readString(input);
    if (material.empty()) throw std::runtime_error("Mesh '" + meshName + "' has an empty material key.");
    materialKeys.insert(material);

    const std::uint32_t vertexStreamCount = readU32(input);
    for (std::uint32_t stream = 0; stream < vertexStreamCount; ++stream) (void)readU32(input);
    (void)readU32(input);  // index stream id
  }
  if (static_cast<std::uint32_t>(input.tellg()) != meshes->end)
    throw std::runtime_error("MeshMetadata did not end at its declared offset.");
  return materialKeys;
}

std::string qualify(const std::string& namesp, const std::string& name) {
  return namesp.empty() ? name : namesp + "/" + name;
}

std::string scalar(const YAML::Node& node, const char* key) {
  const YAML::Node value = node[key];
  return value && value.IsScalar() ? value.as<std::string>() : std::string{};
}

YAML::Node sequence(const YAML::Node& node) {
  if (!node || node.IsSequence()) return node;
  YAML::Node result(YAML::NodeType::Sequence);
  result.push_back(node);
  return result;
}

void scanResources(const YAML::Node& parent, const std::string& namesp, ResourceDeclarations& declarations) {
  for (const YAML::Node& resource : sequence(parent["Resource"])) {
    const std::string name = scalar(resource, "name");
    if (name.empty()) continue;

    ResourceDeclaration declaration;
    declaration.type = scalar(resource, "type");
    const YAML::Node definitions = resource["Definitions"];
    if (definitions) declaration.binding = scalar(definitions["Definition"], "Binding");
    const YAML::Node dependencies = resource["DependentResources"];
    const YAML::Node dependentNodes = dependencies ? dependencies["DependentResource"] : YAML::Node{};
    for (const YAML::Node& dependent : sequence(dependentNodes)) {
      const std::string id = scalar(dependent, "id");
      const std::string reference = scalar(dependent, "ref");
      if (!id.empty() && !reference.empty()) declaration.dependents[id] = reference;
    }
    declarations[qualify(namesp, name)] = std::move(declaration);
  }
}

ResourceDeclarations readResourceDeclarations(const std::filesystem::path& resourcePath) {
  const YAML::Node root = YAML::LoadFile(resourcePath.string())["Resources"];
  ResourceDeclarations declarations;
  scanResources(root, "", declarations);
  for (const YAML::Node& namesp : sequence(root["Namespace"])) scanResources(namesp, scalar(namesp, "name"), declarations);
  return declarations;
}

std::string resolveReference(const std::string& owner, const std::string& reference) {
  if (reference.find('/') != std::string::npos) return reference;
  const std::size_t separator = owner.find('/');
  return separator == std::string::npos ? reference : owner.substr(0, separator + 1) + reference;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: legacy_pbr_baseline_tests <NewTrack.mppmodel> <Resources.yaml>\n";
    return 2;
  }

  try {
    const std::set<std::string> materialKeys = readMeshMaterialKeys(argv[1]);
    const ResourceDeclarations declarations = readResourceDeclarations(argv[2]);
    constexpr const char* trackName = "Tracks/NewTrack";
    const auto track = declarations.find(trackName);
    if (track == declarations.end() || track->second.type != "Track")
      throw std::runtime_error("Resources.yaml does not declare Track resource 'Tracks/NewTrack'.");

    const std::map<std::string, std::string> expectedBindings{
        {"Tracks/TrackAsphaltMaterial", "Track.Asphalt"},
        {"Tracks/DefaultRailMaterial", "Track.Rail"},
        {"Tracks/DefaultMeshMaterial", "Track.Mesh"},
        {"Tracks/DefaultShellMaterial", "Track.Shell"},
        {"Tracks/DefaultZoneMaterial", "Track.Zone"},
        {"Tracks/DefaultTriggerMaterial", "Track.Trigger"},
        {"ModelTool.DefaultFallbackMaterial3D", "Track.Fallback"},
    };

    bool failed = false;
    const auto game = declarations.find("TungstenMonoxide");
    if (game == declarations.end() || game->second.type != "Game") {
      std::cerr << "FAIL: Resources.yaml does not declare Game resource 'TungstenMonoxide'.\n";
      failed = true;
    } else {
      const auto shipMapping = game->second.dependents.find("ShipMaterial");
      if (shipMapping == game->second.dependents.end()) {
        std::cerr << "FAIL: Game has no ShipMaterial dependent resource.\n";
        failed = true;
      } else {
        const auto target = declarations.find(resolveReference("TungstenMonoxide", shipMapping->second));
        if (target == declarations.end() || target->second.type != "PbrMaterialBinding" ||
            target->second.binding != "Ship.Surface") {
          std::cerr << "FAIL: ShipMaterial does not resolve to PbrMaterialBinding 'Ship.Surface'.\n";
          failed = true;
        }
      }
    }

    for (const auto& [key, expectedBinding] : expectedBindings) {
      const auto mapping = track->second.dependents.find(key);
      if (mapping == track->second.dependents.end()) {
        std::cerr << "FAIL: NewTrack.mppmodel material key '" << key
                  << "' has no matching Tracks/NewTrack DependentResource id.\n";
        failed = true;
        continue;
      }

      const std::string targetName = resolveReference(trackName, mapping->second);
      const auto target = declarations.find(targetName);
      if (target == declarations.end()) {
        std::cerr << "FAIL: material key '" << key << "' maps to missing resource '" << targetName << "'.\n";
        failed = true;
      } else if (target->second.type != "PbrMaterialBinding") {
        std::cerr << "FAIL: material key '" << key << "' maps to '" << targetName << "' of type '"
                  << target->second.type << "', expected PbrMaterialBinding.\n";
        failed = true;
      } else if (target->second.binding != expectedBinding) {
        std::cerr << "FAIL: material key '" << key << "' resolves to logical binding '"
                  << target->second.binding << "', expected '" << expectedBinding << "'.\n";
        failed = true;
      }
    }

    for (const std::string& key : materialKeys) {
      if (!expectedBindings.contains(key)) {
        std::cerr << "FAIL: NewTrack.mppmodel contains unexpected material key '" << key << "'.\n";
        failed = true;
      }
    }

    if (materialKeys.empty()) {
      std::cerr << "FAIL: NewTrack.mppmodel contains no mesh material keys.\n";
      failed = true;
    }

    const std::set<std::string> removedLegacyTypes{"Material", "Program", "Shader", "TrackMaterial"};
    for (const auto& [name, declaration] : declarations) {
      if (removedLegacyTypes.contains(declaration.type)) {
        std::cerr << "FAIL: legacy resource '" << name << "' still has type '" << declaration.type << "'.\n";
        failed = true;
      }
      for (const auto& [id, reference] : declaration.dependents) {
        if (id.starts_with("Legacy.") || reference.find("TrackProgram") != std::string::npos) {
          std::cerr << "FAIL: resource '" << name << "' retains legacy dependency '" << id << "'.\n";
          failed = true;
        }
      }
    }
    if (failed) return 1;

    std::cout << "Validated legacy-free PBR resources, " << expectedBindings.size()
              << " stable Tracks/NewTrack bindings, and " << materialKeys.size()
              << " embedded model material keys.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
