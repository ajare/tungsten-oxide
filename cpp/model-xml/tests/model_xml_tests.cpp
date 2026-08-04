// model_xml_tests.cpp — headless tests for ModelXml.cpp (docs/TRACK_MODEL_LIST_PLAN.md Milestone 2):
// parse/write of the <Model> fragment shape, standalone round-trip, and the "Type=Track needs
// TrackData" validation rule.
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

#include "ModelXml.hpp"
#include "willpower/common/tinyxml2.h"

using namespace tinyxml2;
using namespace modelxml;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

ModelXmlDefinition sampleModel(std::optional<std::string> id = std::nullopt) {
  ModelXmlDefinition def;
  def.id = std::move(id);
  def.modelFile = "New_Track.mppmodel";
  def.trackData = "New_Track.json";
  def.meshes.push_back({"path-0-surface", MeshType::Track, true});
  def.meshes.push_back({"path-0-rail-left", MeshType::Track, true});
  def.meshes.push_back({"prop-cube", MeshType::Physical, false});
  return def;
}

void testMeshTypeRoundTrip() {
  check(meshTypeFromString(meshTypeToString(MeshType::Track)) == MeshType::Track, "Track round-trips through its string form");
  check(meshTypeFromString(meshTypeToString(MeshType::Physical)) == MeshType::Physical, "Physical round-trips through its string form");
  check(meshTypeFromString(meshTypeToString(MeshType::Decorative)) == MeshType::Decorative, "Decorative round-trips through its string form");
  check(meshTypeFromString("") == MeshType::Physical, "unrecognized/missing Type text defaults to Physical");
  check(meshTypeFromString("Nonsense") == MeshType::Physical, "unrecognized Type text defaults to Physical");
}

void testValidationRule() {
  ModelXmlDefinition missingTrackData;
  missingTrackData.modelFile = "Cube.mppmodel";
  missingTrackData.meshes.push_back({"road", MeshType::Track, true});
  bool threw = false;
  try {
    validateModelDefinition(missingTrackData);
  } catch (const std::exception&) {
    threw = true;
  }
  check(threw, "a Type=Track mesh with no TrackData fails validation");

  ModelXmlDefinition physicalOnly;
  physicalOnly.modelFile = "Cube.mppmodel";
  physicalOnly.meshes.push_back({"main", MeshType::Physical, true});
  bool physicalThrew = false;
  try {
    validateModelDefinition(physicalOnly);
  } catch (const std::exception&) {
    physicalThrew = true;
  }
  check(!physicalThrew, "a Physical-only Model needs no TrackData");

  ModelXmlDefinition noModelFile;
  bool noModelFileThrew = false;
  try {
    validateModelDefinition(noModelFile);
  } catch (const std::exception&) {
    noModelFileThrew = true;
  }
  check(noModelFileThrew, "an empty ModelFile fails validation");
}

void testWriteThenParseFragment() {
  const ModelXmlDefinition original = sampleModel("model-1");

  XMLDocument document;
  XMLElement* root = document.NewElement("Model");
  document.InsertEndChild(root);
  writeModelFragment(root, original);

  const ModelXmlDefinition parsed = parseModelFragment(root);
  check(parsed.id.has_value() && *parsed.id == "model-1", "id round-trips when present on the element");
  check(parsed.modelFile == original.modelFile, "modelFile round-trips");
  check(parsed.trackData.has_value() && *parsed.trackData == *original.trackData, "trackData round-trips");
  check(parsed.meshes.size() == original.meshes.size(), "mesh count round-trips");
  for (std::size_t i = 0; i < original.meshes.size() && i < parsed.meshes.size(); ++i) {
    check(parsed.meshes[i].name == original.meshes[i].name, "mesh name round-trips: " + original.meshes[i].name);
    check(parsed.meshes[i].type == original.meshes[i].type, "mesh type round-trips: " + original.meshes[i].name);
    check(parsed.meshes[i].visible == original.meshes[i].visible, "mesh visible round-trips: " + original.meshes[i].name);
  }
}

void testEmbeddedInTrackResourceShape() {
  // Two <Model> entries nested inside <Definition><Models>, mirroring
  // D:\Code\Projects\example_track_def.xml exactly -- proves parseModelFragment/writeModelFragment
  // work identically whether the element is a document root or nested arbitrarily deep.
  XMLDocument document;
  XMLElement* definition = document.NewElement("Definition");
  definition->SetAttribute("factory", "Track");
  document.InsertEndChild(definition);
  XMLElement* models = document.NewElement("Models");
  definition->InsertEndChild(models);

  XMLElement* trackModelElem = document.NewElement("Model");
  models->InsertEndChild(trackModelElem);
  writeModelFragment(trackModelElem, sampleModel("track-model"));

  ModelXmlDefinition prop;
  prop.id = "cube-model";
  prop.modelFile = "Cube.mppmodel";
  prop.meshes.push_back({"main", MeshType::Physical, true});
  XMLElement* propModelElem = document.NewElement("Model");
  models->InsertEndChild(propModelElem);
  writeModelFragment(propModelElem, prop);

  int count = 0;
  for (XMLElement* modelElem = models->FirstChildElement("Model"); modelElem != nullptr;
       modelElem = modelElem->NextSiblingElement("Model")) {
    const ModelXmlDefinition parsed = parseModelFragment(modelElem);
    check(parsed.id.has_value(), "embedded Model retains its id: " + std::to_string(count));
    ++count;
  }
  check(count == 2, "both embedded Model entries parse back out of Definition/Models");
}

void testStandaloneRoundTrip() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "model_xml_tests_standalone.xml";
  const ModelXmlDefinition original = sampleModel("should-be-ignored");

  saveStandaloneModelXml(path, original);
  const ModelXmlDefinition loaded = loadStandaloneModelXml(path);

  check(!loaded.id.has_value(), "standalone save/load never carries an id, even if the caller set one");
  check(loaded.modelFile == original.modelFile, "standalone modelFile round-trips");
  check(loaded.trackData.has_value() && *loaded.trackData == *original.trackData, "standalone trackData round-trips");
  check(loaded.meshes.size() == original.meshes.size(), "standalone mesh count round-trips");

  XMLDocument raw;
  check(raw.LoadFile(path.string().c_str()) == XML_SUCCESS, "the standalone file parses as XML");
  if (raw.RootElement() != nullptr) {
    check(std::string(raw.RootElement()->Name()) == "Model", "standalone file's root element is bare <Model>");
    check(raw.RootElement()->Attribute("id") == nullptr, "standalone file's <Model> element has no id attribute at all");
  }

  std::remove(path.string().c_str());
}

}  // namespace

int main() {
  testMeshTypeRoundTrip();
  testValidationRule();
  testWriteThenParseFragment();
  testEmbeddedInTrackResourceShape();
  testStandaloneRoundTrip();

  if (failures) {
    std::cerr << failures << " model_xml test(s) failed\n";
    return 1;
  }
  std::cout << "PASS: model-xml <Model> fragment parse/write/standalone round trip\n";
  return 0;
}
