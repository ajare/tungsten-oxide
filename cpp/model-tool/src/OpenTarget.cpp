#include "OpenTarget.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "willpower/common/tinyxml2.h"

namespace modeltool {
namespace {

using namespace tinyxml2;

bool hasExtension(const std::filesystem::path& path, const char* extensionLowercase) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == extensionLowercase;
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Could not open '" + path.string() + "'.");
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string attr(const XMLElement* element, const char* name) {
  const char* value = element != nullptr ? element->Attribute(name) : nullptr;
  return value != nullptr ? value : "";
}

std::string text(const XMLElement* element) {
  const char* value = element != nullptr ? element->GetText() : nullptr;
  return value != nullptr ? value : "";
}

// Walks <Resources><Namespace name="Tracks"><Resource type="Track"><Definitions><Definition
// factory="Track"><Models>, matching cpp/editor's own TrackResourceDocument.cpp shape. Returns the
// first <Models> element found, or nullptr.
const XMLElement* findModelsList(const XMLDocument& document) {
  const XMLElement* root = document.RootElement();
  if (root == nullptr || std::string(root->Name()) != "Resources") return nullptr;
  for (const XMLElement* namesp = root->FirstChildElement("Namespace"); namesp != nullptr;
       namesp = namesp->NextSiblingElement("Namespace")) {
    if (attr(namesp, "name") != "Tracks") continue;
    for (const XMLElement* resource = namesp->FirstChildElement("Resource"); resource != nullptr;
         resource = resource->NextSiblingElement("Resource")) {
      if (attr(resource, "type") != "Track") continue;
      const XMLElement* definitions = resource->FirstChildElement("Definitions");
      for (const XMLElement* definition = definitions != nullptr ? definitions->FirstChildElement("Definition") : nullptr;
           definition != nullptr; definition = definition->NextSiblingElement("Definition")) {
        if (attr(definition, "factory") != "Track") continue;
        const XMLElement* models = definition->FirstChildElement("Models");
        if (models != nullptr) return models;
      }
    }
  }
  return nullptr;
}

XMLElement* findModelsList(XMLDocument& document) {
  return const_cast<XMLElement*>(findModelsList(const_cast<const XMLDocument&>(document)));
}

}  // namespace

OpenTargetKind classifyOpenTarget(const std::filesystem::path& path) {
  if (hasExtension(path, ".mppmodel")) return OpenTargetKind::MppModel;
  if (!hasExtension(path, ".xml")) return OpenTargetKind::Unsupported;

  std::string xml;
  try {
    xml = readFile(path);
  } catch (const std::exception&) {
    return OpenTargetKind::Unsupported;
  }

  XMLDocument document;
  if (document.Parse(xml.c_str(), xml.size()) != XML_SUCCESS) return OpenTargetKind::Unsupported;
  const XMLElement* root = document.RootElement();
  if (root == nullptr) return OpenTargetKind::Unsupported;

  const std::string rootName = root->Name();
  if (rootName == "Model") return OpenTargetKind::StandaloneModelXml;
  if (rootName == "Resources" && findModelsList(document) != nullptr) return OpenTargetKind::TrackResourceXml;
  return OpenTargetKind::Unsupported;
}

std::vector<TrackResourceModelEntry> scanTrackResourceModels(const std::filesystem::path& xmlPath) {
  const std::string xml = readFile(xmlPath);
  XMLDocument document;
  if (document.Parse(xml.c_str(), xml.size()) != XML_SUCCESS)
    throw std::runtime_error("Invalid Track resource XML '" + xmlPath.string() + "'.");

  const XMLElement* models = findModelsList(document);
  if (models == nullptr) throw std::runtime_error("No Definition[factory=Track]/Models list found in '" + xmlPath.string() + "'.");

  std::vector<TrackResourceModelEntry> result;
  for (const XMLElement* modelElem = models->FirstChildElement("Model"); modelElem != nullptr;
       modelElem = modelElem->NextSiblingElement("Model")) {
    TrackResourceModelEntry entry;
    entry.id = attr(modelElem, "id");
    entry.modelFileReference = text(modelElem->FirstChildElement("ModelFile"));
    result.push_back(std::move(entry));
  }
  return result;
}

modelxml::ModelXmlDefinition readEmbeddedModel(const std::filesystem::path& xmlPath, const std::string& modelId) {
  const std::string xml = readFile(xmlPath);
  XMLDocument document;
  if (document.Parse(xml.c_str(), xml.size()) != XML_SUCCESS)
    throw std::runtime_error("Invalid Track resource XML '" + xmlPath.string() + "'.");

  const XMLElement* models = findModelsList(document);
  for (const XMLElement* modelElem = models != nullptr ? models->FirstChildElement("Model") : nullptr; modelElem != nullptr;
       modelElem = modelElem->NextSiblingElement("Model")) {
    if (attr(modelElem, "id") == modelId) return modelxml::parseModelFragment(modelElem);
  }
  throw std::runtime_error("No <Model id=\"" + modelId + "\"> found in '" + xmlPath.string() + "'.");
}

void rewriteEmbeddedModel(const std::filesystem::path& xmlPath, const std::string& modelId, const modelxml::ModelXmlDefinition& def) {
  const std::string xml = readFile(xmlPath);
  XMLDocument document;
  if (document.Parse(xml.c_str(), xml.size()) != XML_SUCCESS)
    throw std::runtime_error("Invalid Track resource XML '" + xmlPath.string() + "'.");

  XMLElement* models = findModelsList(document);
  XMLElement* target = nullptr;
  for (XMLElement* modelElem = models != nullptr ? models->FirstChildElement("Model") : nullptr; modelElem != nullptr;
       modelElem = modelElem->NextSiblingElement("Model")) {
    if (attr(modelElem, "id") == modelId) {
      target = modelElem;
      break;
    }
  }
  if (target == nullptr) throw std::runtime_error("No <Model id=\"" + modelId + "\"> found in '" + xmlPath.string() + "'.");

  target->DeleteChildren();
  modelxml::writeModelFragment(target, def);

  XMLPrinter printer;
  document.Print(&printer);
  std::ofstream output(xmlPath, std::ios::binary);
  if (!output) throw std::runtime_error("Could not open '" + xmlPath.string() + "' for writing.");
  output << printer.CStr();
}

}  // namespace modeltool
