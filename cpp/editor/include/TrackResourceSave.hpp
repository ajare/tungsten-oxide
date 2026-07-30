// TrackResourceSave.hpp — three-file transactional save planning for native track resources.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "EditorTrackDefinition.hpp"
#include "Track.hpp"

namespace editor {

struct TrackSaveBinding {
  std::filesystem::path xmlPath;
  std::string resourceName;
  std::string trackDataReference;
  std::string modelFileReference;
  std::string resourceFingerprint;
  std::string jsonFingerprint;
};

enum class TrackSaveErrorKind { None,
                                Validation,
                                ExternalXmlConflict,
                                ExternalJsonConflict };

struct TrackSavePlan {
  std::filesystem::path xmlPath, jsonPath, modelPath;
  std::string xml, json, model;
  TrackSaveBinding resultingBinding;
  std::vector<std::string> overwriteWarnings;
  TrackSaveErrorKind errorKind{TrackSaveErrorKind::None};
  std::string error;

  bool ok() const { return error.empty(); }
  bool requiresConfirmation() const { return !overwriteWarnings.empty(); }
};

// `saveAs` always creates standard sidecar names beside xmlPath. A normal bound Save preserves the
// binding's safe relative sidecar references. This function performs no writes.
TrackSavePlan prepareTrackSave(const TrackDefinition& track, const tox::Track& bakedTrack,
                               const std::map<std::string, std::string>& trackMaterialToMaterial,
                               const std::filesystem::path& xmlPath,
                               const std::optional<TrackSaveBinding>& binding, bool saveAs);

// Installs XML, JSON and model as one rollback-capable transaction. On success, all three final
// paths contain the plan's bytes; on failure, pre-existing destinations are restored.
bool commitTrackSave(const TrackSavePlan& plan, std::string& error);

std::string sanitizeTrackResourceFilenameStem(const std::string& name);

}  // namespace editor
