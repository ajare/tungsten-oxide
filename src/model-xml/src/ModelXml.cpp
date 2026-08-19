#include "ModelXml.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "willpower/common/tinyxml2.h"

namespace modelxml {
namespace {

using namespace tinyxml2;

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Could not open '" + path.string() + "'.");
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string xmlError(const XMLDocument& document, const std::filesystem::path& path = {}) {
  std::string result = "Invalid Model XML";
  if (!path.empty()) result += " '" + path.string() + "'";
  if (document.GetErrorStr1() != nullptr && *document.GetErrorStr1() != '\0')
    result += ": " + std::string(document.GetErrorStr1());
  if (document.GetErrorStr2() != nullptr && *document.GetErrorStr2() != '\0')
    result += " " + std::string(document.GetErrorStr2());
  return result;
}

std::string attr(const XMLElement* element, const char* name) {
  const char* value = element != nullptr ? element->Attribute(name) : nullptr;
  return value != nullptr ? value : "";
}

std::string text(const XMLElement* element) {
  const char* value = element != nullptr ? element->GetText() : nullptr;
  return value != nullptr ? value : "";
}

bool truthy(const std::string& value) {
  return value == "true" || value == "1" || value == "yes";
}

XMLElement* addTextChild(XMLDocument& document, XMLElement* parent, const char* name, const std::string& value) {
  XMLElement* child = document.NewElement(name);
  child->SetText(value.c_str());
  parent->InsertEndChild(child);
  return child;
}

}  // namespace

std::string meshTypeToString(MeshType type) {
  switch (type) {
    case MeshType::Track: return "Track";
    case MeshType::Decorative: return "Decorative";
    case MeshType::Physical:
    default: return "Physical";
  }
}

MeshType meshTypeFromString(const std::string& text) {
  if (text == "Track") return MeshType::Track;
  if (text == "Decorative") return MeshType::Decorative;
  return MeshType::Physical;
}

void validateModelDefinition(const ModelXmlDefinition& def) {
  if (def.modelFile.empty()) throw std::runtime_error("<Model> is missing a non-empty <ModelFile>.");
  for (const auto& mesh : def.meshes) {
    if (mesh.type == MeshType::Track && !def.trackData.has_value())
      throw std::runtime_error("Mesh '" + mesh.name + "' is Type=Track but the Model has no <TrackData>; "
                                "any Model with a Track mesh needs a corresponding TrackData file.");
  }
}

ModelXmlDefinition parseModelFragment(const XMLElement* modelElem) {
  if (modelElem == nullptr) throw std::runtime_error("<Model> element is null.");

  ModelXmlDefinition def;
  const std::string id = attr(modelElem, "id");
  if (!id.empty()) def.id = id;

  def.modelFile = text(modelElem->FirstChildElement("ModelFile"));
  if (def.modelFile.empty()) throw std::runtime_error("<Model> is missing a non-empty <ModelFile>.");

  const std::string trackData = text(modelElem->FirstChildElement("TrackData"));
  if (!trackData.empty()) def.trackData = trackData;

  const XMLElement* meshesElem = modelElem->FirstChildElement("Meshes");
  for (const XMLElement* meshElem = meshesElem != nullptr ? meshesElem->FirstChildElement("Mesh") : nullptr;
       meshElem != nullptr; meshElem = meshElem->NextSiblingElement("Mesh")) {
    MeshMetadataXmlDefinition mesh;
    mesh.name = text(meshElem->FirstChildElement("Name"));
    mesh.type = meshTypeFromString(text(meshElem->FirstChildElement("Type")));
    const XMLElement* visibleElem = meshElem->FirstChildElement("Visible");
    mesh.visible = visibleElem == nullptr || truthy(text(visibleElem));
    def.meshes.push_back(std::move(mesh));
  }

  validateModelDefinition(def);
  return def;
}

void writeModelFragment(XMLElement* modelElem, const ModelXmlDefinition& def) {
  if (modelElem == nullptr) throw std::runtime_error("<Model> element is null.");
  validateModelDefinition(def);

  XMLDocument& document = *modelElem->GetDocument();
  if (def.id.has_value()) modelElem->SetAttribute("id", def.id->c_str());

  addTextChild(document, modelElem, "ModelFile", def.modelFile);
  if (def.trackData.has_value()) addTextChild(document, modelElem, "TrackData", *def.trackData);

  XMLElement* meshesElem = document.NewElement("Meshes");
  modelElem->InsertEndChild(meshesElem);
  for (const auto& mesh : def.meshes) {
    XMLElement* meshElem = document.NewElement("Mesh");
    meshesElem->InsertEndChild(meshElem);
    addTextChild(document, meshElem, "Name", mesh.name);
    addTextChild(document, meshElem, "Type", meshTypeToString(mesh.type));
    addTextChild(document, meshElem, "Visible", mesh.visible ? "true" : "false");
  }
}

ModelXmlDefinition loadStandaloneModelXml(const std::filesystem::path& path) {
  const std::string xml = readFile(path);
  XMLDocument document;
  if (document.Parse(xml.c_str(), xml.size()) != XML_SUCCESS) throw std::runtime_error(xmlError(document, path));
  XMLElement* root = document.RootElement();
  if (root == nullptr || std::string(root->Name()) != "Model")
    throw std::runtime_error("Model XML '" + path.string() + "' root element must be <Model>.");

  ModelXmlDefinition def = parseModelFragment(root);
  def.id.reset();  // Standalone files never own an id (TRACK_MODEL_LIST_PLAN.md).
  return def;
}

void saveStandaloneModelXml(const std::filesystem::path& path, const ModelXmlDefinition& def) {
  ModelXmlDefinition standalone = def;
  standalone.id.reset();  // Never written for a standalone file, even if the caller set one.
  validateModelDefinition(standalone);

  XMLDocument document;
  document.InsertEndChild(document.NewDeclaration());
  XMLElement* root = document.NewElement("Model");
  document.InsertEndChild(root);
  writeModelFragment(root, standalone);

  XMLPrinter printer;
  document.Print(&printer);
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("Could not open '" + path.string() + "' for writing.");
  output << printer.CStr();
}

}  // namespace modelxml
