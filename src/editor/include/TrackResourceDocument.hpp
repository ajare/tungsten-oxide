// TrackResourceDocument.hpp — editor-owned Resources XML discovery and Track-resource upsert.
// JSON remains the editable source; .mppmodel and the primary Model's own <Meshes> list are
// generated on save.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "EditorTrackDefinition.hpp"
#include "ModelXml.hpp"

namespace editor {

// A Track resource's <Definition><Models> list (TRACK_MODEL_LIST_PLAN.md), one entry per <Model>
// in document order. The editor authors exactly one Track-type model (`models[primaryModelIndex]`
// -- the one carrying the baked road/rail/etc. geometry this session edits) and treats every other
// entry as opaque pass-through data: preserved verbatim on Save (Milestone 5.3), never edited here
// -- adding NEW non-primary entries is Milestone 6's "Load Model" job, not this one's.
struct TrackResourceCandidate {
  std::string resourceName;
  // Mirror the primary (first Type=Track) entry in `models` for minimal disruption to existing
  // call sites that predate the <Models> list -- kept in sync with `models[primaryModelIndex]`.
  std::string trackDataReference;
  std::string modelFileReference;
  std::filesystem::path trackDataPath;
  std::filesystem::path modelFilePath;
  std::string resourceFingerprint;
  std::string jsonFingerprint;
  std::optional<TrackDefinition> track;
  std::string error;
  std::string warning;

  std::vector<modelxml::ModelXmlDefinition> models;
  std::size_t primaryModelIndex{0};

  bool loadable() const { return track.has_value() && error.empty(); }
};

struct TrackResourceScanResult {
  std::vector<TrackResourceCandidate> tracks;
  std::string error;
  bool validDocument() const { return error.empty(); }
};

// Reads all direct Resource[type=Track] children across every root-level
// Namespace[name=Tracks]. Invalid resources remain in the result with `error` set.
TrackResourceScanResult scanTrackResources(const std::filesystem::path& xmlPath);

struct TrackResourceUpsertResult {
  std::string xml;
  bool replacedExisting{false};
  std::string error;
};

// Inserts/replaces a complete generated Resource element while retaining every unrelated XML
// node. `resourceDocumentXml` must be empty for a new document, or a valid <Resources> document.
TrackResourceUpsertResult upsertTrackResource(const std::string& resourceDocumentXml,
                                              const std::string& generatedStandaloneTrackXml,
                                              const std::string& resourceName);

// Safe resource-relative paths are non-empty, non-absolute and remain below the XML directory
// after lexical normalization. Backslashes are accepted on Windows and retained in XML text.
bool isSafeResourceRelativePath(const std::string& reference);
std::filesystem::path resolveResourceRelativePath(const std::filesystem::path& xmlPath,
                                                  const std::string& reference);

// Canonical element fingerprint used to detect external changes to the bound Track resource.
std::optional<std::string> findTrackResourceFingerprint(const std::string& resourceDocumentXml,
                                                        const std::string& resourceName,
                                                        std::string& error);

}  // namespace editor
