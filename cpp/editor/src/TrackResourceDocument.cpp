#include "TrackResourceDocument.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>

#include "willpower/common/tinyxml2.h"

#include "FileDialog.hpp"

namespace editor {
namespace {

using namespace tinyxml2;

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Could not open '" + pathToUtf8(path) + "'.");
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string xmlError(const XMLDocument& document, const std::filesystem::path& path = {}) {
  std::string result = "Invalid Resources XML";
  if (!path.empty()) result += " '" + pathToUtf8(path) + "'";
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

std::string fingerprint(const XMLElement* element) {
  XMLPrinter printer(nullptr, true);
  element->Accept(&printer);
  return printer.CStr();
}

XMLNode* cloneTree(const XMLNode* source, XMLDocument& destination) {
  XMLNode* copy = source->ShallowClone(&destination);
  for (const XMLNode* child = source->FirstChild(); child != nullptr; child = child->NextSibling())
    copy->InsertEndChild(cloneTree(child, destination));
  return copy;
}

std::vector<XMLElement*> tracksNamed(XMLDocument& document, const std::string& name,
                                     bool* sameNameOtherType = nullptr) {
  std::vector<XMLElement*> result;
  if (sameNameOtherType != nullptr) *sameNameOtherType = false;
  XMLElement* root = document.RootElement();
  if (root == nullptr) return result;
  for (XMLElement* namesp = root->FirstChildElement("Namespace"); namesp != nullptr;
       namesp = namesp->NextSiblingElement("Namespace")) {
    if (attr(namesp, "name") != "Tracks") continue;
    for (XMLElement* resource = namesp->FirstChildElement("Resource"); resource != nullptr;
         resource = resource->NextSiblingElement("Resource")) {
      if (attr(resource, "name") != name) continue;
      if (attr(resource, "type") == "Track")
        result.push_back(resource);
      else if (sameNameOtherType != nullptr)
        *sameNameOtherType = true;
    }
  }
  return result;
}

XMLElement* firstTracksNamespace(XMLDocument& document) {
  XMLElement* root = document.RootElement();
  for (XMLElement* namesp = root != nullptr ? root->FirstChildElement("Namespace") : nullptr;
       namesp != nullptr; namesp = namesp->NextSiblingElement("Namespace"))
    if (attr(namesp, "name") == "Tracks") return namesp;
  return nullptr;
}

TrackResourceCandidate parseCandidate(XMLElement* resource, const std::filesystem::path& xmlPath) {
  TrackResourceCandidate out;
  out.resourceName = attr(resource, "name");
  out.resourceFingerprint = fingerprint(resource);
  if (out.resourceName.empty()) {
    out.resourceName = "(unnamed Track resource)";
    out.error = "Missing Resource name attribute.";
    return out;
  }

  XMLElement* definitions = resource->FirstChildElement("Definitions");
  XMLElement* trackDefinition = nullptr;
  int definitionCount = 0;
  for (XMLElement* definition = definitions != nullptr ? definitions->FirstChildElement("Definition") : nullptr;
       definition != nullptr; definition = definition->NextSiblingElement("Definition")) {
    if (attr(definition, "factory") != "Track") continue;
    trackDefinition = definition;
    ++definitionCount;
  }
  if (definitionCount != 1) {
    out.error = definitionCount == 0 ? "Missing Definition factory=\"Track\"." : "Contains multiple Definition factory=\"Track\" elements.";
    return out;
  }

  // <Models> list (TRACK_MODEL_LIST_PLAN.md): a clean break from the old bare <TrackData>/
  // <ModelFile> pair, no migration -- see this header's own comment. Every <Model> is parsed via
  // cpp/model-xml (shared with model-tool), then the first Type=Track entry becomes "primary."
  XMLElement* modelsElem = trackDefinition->FirstChildElement("Models");
  if (modelsElem == nullptr) {
    out.error = "Missing <Models> list (old bare <TrackData>/<ModelFile> Track resources are no "
               "longer supported -- re-save with a current editor).";
    return out;
  }
  for (XMLElement* modelElem = modelsElem->FirstChildElement("Model"); modelElem != nullptr;
       modelElem = modelElem->NextSiblingElement("Model")) {
    try {
      out.models.push_back(modelxml::parseModelFragment(modelElem));
    } catch (const std::exception& error) {
      out.error = std::string("Invalid <Model> in <Models>: ") + error.what();
      return out;
    }
  }
  const auto primaryIt = std::find_if(out.models.begin(), out.models.end(), [](const modelxml::ModelXmlDefinition& model) {
    return std::any_of(model.meshes.begin(), model.meshes.end(),
                       [](const modelxml::MeshMetadataXmlDefinition& mesh) { return mesh.type == modelxml::MeshType::Track; });
  });
  if (primaryIt == out.models.end() || !primaryIt->trackData.has_value()) {
    out.error = "No Track-type Model with <TrackData> found in <Models>.";
    return out;
  }
  out.primaryModelIndex = static_cast<std::size_t>(primaryIt - out.models.begin());

  out.trackDataReference = *primaryIt->trackData;
  if (!isSafeResourceRelativePath(out.trackDataReference)) {
    out.error = "TrackData must be a safe relative path within the Resources XML directory.";
    return out;
  }
  out.trackDataPath = resolveResourceRelativePath(xmlPath, out.trackDataReference);

  out.modelFileReference = primaryIt->modelFile;
  if (isSafeResourceRelativePath(out.modelFileReference)) {
    out.modelFilePath = resolveResourceRelativePath(xmlPath, out.modelFileReference);
    if (!std::filesystem::exists(out.modelFilePath)) out.warning = "ModelFile is missing and will be regenerated on Save.";
  } else {
    out.modelFileReference.clear();
    out.modelFilePath.clear();
    out.warning = "ModelFile is missing or unsafe and will be repaired on Save.";
  }

  try {
    out.jsonFingerprint = readFile(out.trackDataPath);
    out.track = fromJson(out.jsonFingerprint);
    // Seed the editable document's own `models` (TRACK_MODEL_LIST_PLAN.md Milestone 6) with every
    // non-primary entry -- the primary is regenerated fresh from the bake every save (see
    // TrackResourceSave.cpp) and never lives here, only in the `models`/`primaryModelIndex` pair
    // above.
    if (out.track.has_value())
      for (std::size_t i = 0; i < out.models.size(); ++i)
        if (i != out.primaryModelIndex) out.track->models.push_back(out.models[i]);
  } catch (const std::exception& error) {
    out.error = error.what();
  }
  return out;
}

bool parseResourcesDocument(const std::string& xml, XMLDocument& document, std::string& error) {
  if (document.Parse(xml.c_str(), xml.size()) != XML_SUCCESS) {
    error = xmlError(document);
    return false;
  }
  XMLElement* root = document.RootElement();
  if (root == nullptr || std::string(root->Name()) != "Resources") {
    error = "XML root element must be <Resources>.";
    return false;
  }
  return true;
}

}  // namespace

bool isSafeResourceRelativePath(const std::string& reference) {
  if (reference.empty()) return false;
  const std::filesystem::path path = std::filesystem::path(utf8ToWide(reference));
  if (path.empty() || path == "." || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
  for (const auto& part : path)
    if (part == "..") return false;
  return true;
}

std::filesystem::path resolveResourceRelativePath(const std::filesystem::path& xmlPath,
                                                  const std::string& reference) {
  return (xmlPath.parent_path() / std::filesystem::path(utf8ToWide(reference))).lexically_normal();
}

TrackResourceScanResult scanTrackResources(const std::filesystem::path& xmlPath) {
  TrackResourceScanResult result;
  std::string xml;
  try {
    xml = readFile(xmlPath);
  } catch (const std::exception& error) {
    result.error = error.what();
    return result;
  }

  XMLDocument document;
  if (document.Parse(xml.c_str(), xml.size()) != XML_SUCCESS) {
    result.error = xmlError(document, xmlPath);
    return result;
  }
  XMLElement* root = document.RootElement();
  if (root == nullptr || std::string(root->Name()) != "Resources") {
    result.error = "XML root element must be <Resources>.";
    return result;
  }

  std::map<std::string, int> identityCounts;
  for (XMLElement* namesp = root->FirstChildElement("Namespace"); namesp != nullptr;
       namesp = namesp->NextSiblingElement("Namespace")) {
    if (attr(namesp, "name") != "Tracks") continue;
    for (XMLElement* resource = namesp->FirstChildElement("Resource"); resource != nullptr;
         resource = resource->NextSiblingElement("Resource")) {
      const std::string name = attr(resource, "name");
      if (!name.empty()) ++identityCounts[name];
      if (attr(resource, "type") == "Track") result.tracks.push_back(parseCandidate(resource, xmlPath));
    }
  }
  for (auto& candidate : result.tracks) {
    if (identityCounts[candidate.resourceName] > 1)
      candidate.error = "Resource identity Tracks/" + candidate.resourceName + " is declared more than once.";
  }
  return result;
}

TrackResourceUpsertResult upsertTrackResource(const std::string& resourceDocumentXml,
                                              const std::string& generatedStandaloneTrackXml,
                                              const std::string& resourceName) {
  TrackResourceUpsertResult result;
  XMLDocument target;
  if (resourceDocumentXml.empty()) {
    target.InsertEndChild(target.NewDeclaration());
    target.InsertEndChild(target.NewElement("Resources"));
  } else if (!parseResourcesDocument(resourceDocumentXml, target, result.error)) {
    return result;
  }

  XMLDocument generated;
  std::string generatedError;
  if (!parseResourcesDocument(generatedStandaloneTrackXml, generated, generatedError)) {
    result.error = "Generated Track XML is invalid: " + generatedError;
    return result;
  }
  XMLElement* generatedNamespace = firstTracksNamespace(generated);
  XMLElement* generatedResource = generatedNamespace != nullptr ? generatedNamespace->FirstChildElement("Resource") : nullptr;
  if (generatedResource == nullptr || attr(generatedResource, "type") != "Track" ||
      attr(generatedResource, "name") != resourceName) {
    result.error = "Generated XML does not contain the expected Tracks/" + resourceName + " Track resource.";
    return result;
  }

  bool sameNameOtherType = false;
  std::vector<XMLElement*> matches = tracksNamed(target, resourceName, &sameNameOtherType);
  if (sameNameOtherType) {
    result.error = "Tracks/" + resourceName + " is already used by a non-Track resource.";
    return result;
  }
  if (matches.size() > 1) {
    result.error = "Tracks/" + resourceName + " is declared more than once.";
    return result;
  }

  XMLElement* destinationNamespace = firstTracksNamespace(target);
  if (destinationNamespace == nullptr) {
    destinationNamespace = target.NewElement("Namespace");
    destinationNamespace->SetAttribute("name", "Tracks");
    target.RootElement()->InsertEndChild(destinationNamespace);
  }
  XMLNode* replacement = cloneTree(generatedResource, target);
  if (matches.empty()) {
    destinationNamespace->InsertEndChild(replacement);
  } else {
    XMLNode* old = matches.front();
    old->Parent()->InsertAfterChild(old, replacement);
    old->Parent()->DeleteChild(old);
    result.replacedExisting = true;
  }

  XMLPrinter printer(nullptr, false);
  target.Print(&printer);
  result.xml = printer.CStr();

  XMLDocument verification;
  if (!parseResourcesDocument(result.xml, verification, result.error)) result.xml.clear();
  return result;
}

std::optional<std::string> findTrackResourceFingerprint(const std::string& resourceDocumentXml,
                                                        const std::string& resourceName,
                                                        std::string& error) {
  XMLDocument document;
  if (!parseResourcesDocument(resourceDocumentXml, document, error)) return std::nullopt;
  bool sameNameOtherType = false;
  const auto matches = tracksNamed(document, resourceName, &sameNameOtherType);
  if (sameNameOtherType) {
    error = "Tracks/" + resourceName + " is also used by a non-Track resource.";
    return std::nullopt;
  }
  if (matches.size() != 1) {
    error = matches.empty() ? "Tracks/" + resourceName + " no longer exists." : "Tracks/" + resourceName + " is declared more than once.";
    return std::nullopt;
  }
  return fingerprint(matches.front());
}

}  // namespace editor
