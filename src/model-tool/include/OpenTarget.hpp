// OpenTarget.hpp — classifies what model-tool's "Open" dialog was pointed at (TRACK_MODEL_LIST_PLAN.md
// Milestone 3.3), and the minimal Track-resource-XML tree-walking needed to locate/rewrite exactly
// one embedded <Model> element by id. Deliberately independent of src/editor's own
// TrackResourceDocument.cpp/EditorTrackDefinition.hpp -- model-tool only ever needs to find a
// <Models> list to pick from and rewrite one <Model> fragment in place, not the editor's whole
// authoring/undo apparatus, so this re-walks the documented `<Resources><Namespace name="Tracks">
// <Resource type="Track"><Definitions><Definition factory="Track"><Models>` shape itself rather than
// depending on editor code (matching this codebase's existing "each consumer reimplements XML
// parsing independently" convention -- see src/tungsten-monoxide's own wp::XmlNode parser).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ModelXml.hpp"

namespace modeltool {

enum class OpenTargetKind {
  MppModel,           // .mppmodel extension -- unchanged existing raw-model open path
  StandaloneModelXml,  // .xml file whose root element is <Model>
  TrackResourceXml,    // .xml file containing a Resources/Namespace[Tracks]/Resource[Track]/
                       // Definitions/Definition[factory=Track]/Models list
  Unsupported          // anything else -- falls through to the existing AssImp import path
};

// Classifies by extension for .mppmodel (matching the existing openPath() dispatch), by sniffing
// the parsed root element for .xml; every other extension is Unsupported (AssImp decides for
// itself whether it can actually import it, same as today).
OpenTargetKind classifyOpenTarget(const std::filesystem::path& path);

struct TrackResourceModelEntry {
  std::string id;
  std::string modelFileReference;  // <ModelFile> text, relative to the XML file's own directory
};

// Scans the first <Definition factory="Track"> found (see the header comment above for the exact
// walk) for its <Models> list. Throws std::runtime_error if the file doesn't parse as XML or no
// such list is found.
std::vector<TrackResourceModelEntry> scanTrackResourceModels(const std::filesystem::path& xmlPath);

// Reads the full fragment (mesh metadata included, not just id/ModelFile) for one embedded
// <Model id=modelId> found via the same walk. Throws if no matching element is found.
modelxml::ModelXmlDefinition readEmbeddedModel(const std::filesystem::path& xmlPath, const std::string& modelId);

// Re-parses `xmlPath`, locates the <Model id=modelId> element inside the same walk
// scanTrackResourceModels() uses, replaces its children with `def`'s (via
// modelxml::writeModelFragment, after DeleteChildren()), and writes the whole document back out --
// every other node in the file is left exactly as it was. Throws if no matching element is found.
void rewriteEmbeddedModel(const std::filesystem::path& xmlPath, const std::string& modelId, const modelxml::ModelXmlDefinition& def);

}  // namespace modeltool
