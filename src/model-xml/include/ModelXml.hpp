// ModelXml.hpp — the <Model> XML fragment schema shared by src/editor and src/model-tool
// (docs/adr/0003-model-xml-layer.md, docs/TRACK_MODEL_LIST_PLAN.md Milestone 2). A fragment
// describes one .mppmodel reference plus optional TrackData and per-mesh Type/Visible metadata:
//
//   <Model id="...">              <!-- id only present when embedded in a Track resource -->
//     <ModelFile>Cube.mppmodel</ModelFile>
//     <TrackData>New_Track.json</TrackData>   <!-- optional; required if any mesh is Type=Track -->
//     <Meshes>
//       <Mesh>
//         <Name>path-0-surface</Name>
//         <Type>Track</Type>       <!-- Track | Physical | Decorative -->
//         <Visible>true</Visible>
//       </Mesh>
//     </Meshes>
//   </Model>
//
// parseModelFragment/writeModelFragment operate on an already-located <Model> element -- the
// caller decides whether that element is a standalone document's root or nested inside a Track
// resource's <Definition><Models> list; both shapes use the exact same fragment format. Every
// function here throws std::runtime_error with a human-readable message on a schema violation
// (matching src/editor/src/TrackResourceDocument.cpp's own convention) rather than returning an
// error code.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tinyxml2 {
class XMLElement;
}

namespace modelxml {

enum class MeshType { Track,
                      Physical,
                      Decorative };

std::string meshTypeToString(MeshType type);
// Unrecognized/missing text normalizes to Physical, matching model-tool's own "no XML metadata yet"
// default (TRACK_MODEL_LIST_PLAN.md Milestone 3.2).
MeshType meshTypeFromString(const std::string& text);

struct MeshMetadataXmlDefinition {
  std::string name;
  MeshType type{MeshType::Physical};
  bool visible{true};
};

struct ModelXmlDefinition {
  // Present only for a Model embedded in a Track resource's <Models> list, generated fresh by the
  // editor at embed-time; a standalone Model XML file never carries one (TRACK_MODEL_LIST_PLAN.md's
  // "Model id origin" decision) -- loadStandaloneModelXml/saveStandaloneModelXml below enforce this.
  std::optional<std::string> id;
  std::string modelFile;
  std::optional<std::string> trackData;
  std::vector<MeshMetadataXmlDefinition> meshes;
};

// Throws if any mesh's Type is Track and `trackData` is unset (TRACK_MODEL_LIST_PLAN.md: "Any Model
// which has a Track mesh will need a corresponding TrackData file") or if `modelFile` is empty.
void validateModelDefinition(const ModelXmlDefinition& def);

// Reads an already-located <Model> element (id attribute read if present). Throws on a missing/
// empty <ModelFile> or a validateModelDefinition() failure.
ModelXmlDefinition parseModelFragment(const tinyxml2::XMLElement* modelElem);

// Writes into an already-located, already-created <Model> element -- id attribute set iff
// def.id.has_value(). Throws on a validateModelDefinition() failure (validated before any element
// is mutated).
void writeModelFragment(tinyxml2::XMLElement* modelElem, const ModelXmlDefinition& def);

// Bare <Model> document root, no <Definition>/<Resources> wrapper. `id` is always empty in the
// result regardless of what the file's root element attribute says (standalone files don't own an
// id -- TRACK_MODEL_LIST_PLAN.md).
ModelXmlDefinition loadStandaloneModelXml(const std::filesystem::path& path);
// `def.id` is ignored (never written), even if set.
void saveStandaloneModelXml(const std::filesystem::path& path, const ModelXmlDefinition& def);

}  // namespace modelxml
