#include "TrackResourceSave.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>

#include "FileDialog.hpp"
#include "MppModelExport.hpp"
#include "TrackResourceDocument.hpp"

namespace editor {
namespace {

std::optional<std::string> readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string defaultResourceName(const TrackDefinition& track) { return track.name.empty() ? "Track" : track.name; }

const TrackResourceCandidate* findCandidate(const TrackResourceScanResult& scan, const std::string& name,
                                            int& count) {
  const TrackResourceCandidate* found = nullptr;
  count = 0;
  for (const auto& candidate : scan.tracks) {
    if (candidate.resourceName != name) continue;
    found = &candidate;
    ++count;
  }
  return found;
}

bool samePath(const std::filesystem::path& a, const std::filesystem::path& b) {
  return a.lexically_normal() == b.lexically_normal();
}

// Mirrors Map.cpp's gameplayKind() / MppModelExport.cpp's <TrackMeshes> filter: every kind a ship
// can physically contact. Any track a ship can drive on must bake at least one such batch, or
// Map::load's own "TrackMeshes produced no collision triangles" check would fail at game-load
// time instead -- catch it here, at export time, where it's still easy to fix.
bool hasCollidableGeometry(const tox::Track& bakedTrack) {
  for (const tox::GeometryBatch& batch : bakedTrack.geometry) {
    switch (batch.kind) {
      case tox::GeometryKind::PathSurface:
      case tox::GeometryKind::MeshSurface:
      case tox::GeometryKind::ReservationWall:
      case tox::GeometryKind::PathRail:
      case tox::GeometryKind::MeshRail:
        return true;
      default:
        break;
    }
  }
  return false;
}

}  // namespace

std::string sanitizeTrackResourceFilenameStem(const std::string& name) {
  const std::string base = name.empty() ? "Track" : name;
  std::string out;
  bool inRun = false;
  for (char c : base) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    if (ok) {
      out += c;
      inRun = false;
    } else if (!inRun) {
      out += '_';
      inRun = true;
    }
  }
  return out.empty() ? "Track" : out;
}

TrackSavePlan prepareTrackSave(const TrackDefinition& track, const tox::Track& bakedTrack,
                               const std::map<std::string, std::string>& trackMaterialToMaterial,
                               const std::filesystem::path& xmlPath,
                               const std::optional<TrackSaveBinding>& binding, bool saveAs) {
  TrackSavePlan plan;
  plan.xmlPath = xmlPath.lexically_normal();
  if (plan.xmlPath.empty()) {
    plan.error = "No Resources XML path was selected.";
    return plan;
  }

  for (const Path& path : track.paths) {
    if (!path.material.empty() && !trackMaterialToMaterial.count(path.material)) {
      plan.error = "Unavailable TrackMaterial: " + path.material;
      return plan;
    }
  }

  if (!hasCollidableGeometry(bakedTrack)) {
    plan.error = "Track has no drivable surface, wall or rail geometry to export -- add a path or mesh region before saving.";
    return plan;
  }

  const std::string resourceName = binding ? binding->resourceName : defaultResourceName(track);
  const std::string stem = sanitizeTrackResourceFilenameStem(resourceName);
  std::string trackDataReference = !saveAs && binding ? binding->trackDataReference : stem + ".json";
  std::string modelFileReference = !saveAs && binding ? binding->modelFileReference : stem + ".mppmodel";
  if (!isSafeResourceRelativePath(trackDataReference)) {
    plan.error = "TrackData is not a safe resource-relative path.";
    return plan;
  }
  if (!isSafeResourceRelativePath(modelFileReference)) modelFileReference = stem + ".mppmodel";

  plan.jsonPath = resolveResourceRelativePath(plan.xmlPath, trackDataReference);
  plan.modelPath = resolveResourceRelativePath(plan.xmlPath, modelFileReference);
  if (samePath(plan.xmlPath, plan.jsonPath) || samePath(plan.xmlPath, plan.modelPath) ||
      samePath(plan.jsonPath, plan.modelPath)) {
    plan.error = "Resources XML, TrackData and ModelFile must resolve to three distinct files.";
    return plan;
  }

  std::string existingXml;
  const bool xmlExists = std::filesystem::exists(plan.xmlPath);
  if (xmlExists) {
    const auto contents = readFile(plan.xmlPath);
    if (!contents) {
      plan.error = "Could not read '" + pathToUtf8(plan.xmlPath) + "'.";
      return plan;
    }
    existingXml = *contents;
    if (std::all_of(existingXml.begin(), existingXml.end(),
                    [](unsigned char c) { return std::isspace(c) != 0; }))
      existingXml.clear();
  }

  TrackResourceScanResult targetScan;
  if (xmlExists && !existingXml.empty()) {
    targetScan = scanTrackResources(plan.xmlPath);
    if (!targetScan.validDocument()) {
      plan.error = targetScan.error;
      return plan;
    }
  }

  if (!saveAs && binding) {
    if (!samePath(plan.xmlPath, binding->xmlPath)) {
      plan.error = "Bound Save target changed unexpectedly; use Save As.";
      return plan;
    }
    std::string fingerprintError;
    const auto currentFingerprint = findTrackResourceFingerprint(existingXml, resourceName, fingerprintError);
    if (!currentFingerprint || *currentFingerprint != binding->resourceFingerprint) {
      plan.errorKind = TrackSaveErrorKind::ExternalXmlConflict;
      plan.error = currentFingerprint ? "The bound Track resource changed outside the editor." : fingerprintError;
      return plan;
    }
    const auto currentJson = readFile(plan.jsonPath);
    if (!currentJson || *currentJson != binding->jsonFingerprint) {
      plan.errorKind = TrackSaveErrorKind::ExternalJsonConflict;
      plan.error = "TrackData changed outside the editor: " + pathToUtf8(plan.jsonPath);
      return plan;
    }
  }

  int matchingCount = 0;
  const TrackResourceCandidate* matching = findCandidate(targetScan, resourceName, matchingCount);
  if (matchingCount > 1) {
    plan.error = "Tracks/" + resourceName + " is declared more than once.";
    return plan;
  }

  if (saveAs && matchingCount == 1)
    plan.overwriteWarnings.push_back("Replace existing Track resource Tracks/" + resourceName);

  const bool jsonOwned = matching != nullptr && !matching->trackDataReference.empty() &&
                         samePath(resolveResourceRelativePath(plan.xmlPath, matching->trackDataReference), plan.jsonPath);
  const bool modelOwned = matching != nullptr && !matching->modelFileReference.empty() &&
                          samePath(resolveResourceRelativePath(plan.xmlPath, matching->modelFileReference), plan.modelPath);
  if (saveAs && std::filesystem::exists(plan.jsonPath) && !jsonOwned)
    plan.overwriteWarnings.push_back("Overwrite unrelated file " + pathToUtf8(plan.jsonPath));
  if (saveAs && std::filesystem::exists(plan.modelPath) && !modelOwned)
    plan.overwriteWarnings.push_back("Overwrite unrelated file " + pathToUtf8(plan.modelPath));

  // The primary Track-type Model's own id, and every other <Model> entry (Physical/Decorative props
  // -- Milestone 6's "Load Model", never touched here), both preserved verbatim from whatever's
  // currently on disk at this Resource identity (`matching`, freshly rescanned above) -- mirrors
  // ADR 0002 D4's "Save replaces one complete Track Resource and preserves the rest," generalized to
  // per-Model granularity within the <Models> list. A brand-new track (no `matching`) gets a fresh
  // id and no other models to preserve.
  std::string primaryModelId = stem;
  std::vector<modelxml::ModelXmlDefinition> otherModels;
  if (matching != nullptr && matching->primaryModelIndex < matching->models.size()) {
    const modelxml::ModelXmlDefinition& existingPrimary = matching->models[matching->primaryModelIndex];
    if (existingPrimary.id.has_value() && !existingPrimary.id->empty()) primaryModelId = *existingPrimary.id;
    for (std::size_t i = 0; i < matching->models.size(); ++i)
      if (i != matching->primaryModelIndex) otherModels.push_back(matching->models[i]);
  }

  plan.json = toJson(track) + "\n";
  const MppModelExportResult exported = exportTrackToMppModel(bakedTrack, trackMaterialToMaterial);
  plan.model = exported.bytes;
  const std::string standalone = buildTrackResourceXmlForName(
      track, bakedTrack, resourceName, modelFileReference, trackDataReference, trackMaterialToMaterial,
      primaryModelId, otherModels);
  const TrackResourceUpsertResult upsert = upsertTrackResource(existingXml, standalone, resourceName);
  if (!upsert.error.empty()) {
    plan.error = upsert.error;
    return plan;
  }
  plan.xml = upsert.xml;

  std::string fingerprintError;
  const auto resourceFingerprint = findTrackResourceFingerprint(plan.xml, resourceName, fingerprintError);
  if (!resourceFingerprint) {
    plan.error = "Generated Track resource could not be verified: " + fingerprintError;
    return plan;
  }
  plan.resultingBinding = {plan.xmlPath, resourceName, trackDataReference, modelFileReference,
                           *resourceFingerprint, plan.json};
  return plan;
}

bool commitTrackSave(const TrackSavePlan& plan, std::string& error) {
  if (!plan.ok()) {
    error = plan.error;
    return false;
  }
  const std::array<std::filesystem::path, 3> finals{plan.modelPath, plan.jsonPath, plan.xmlPath};
  const std::array<const std::string*, 3> contents{&plan.model, &plan.json, &plan.xml};
  std::array<std::filesystem::path, 3> temps, backups;
  std::array<bool, 3> backedUp{false, false, false}, installed{false, false, false};

  std::error_code ec;
  for (const auto& path : finals) {
    if (path.parent_path().empty()) continue;
    ec.clear();
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      error = "Could not create directory for '" + pathToUtf8(path) + "': " + ec.message();
      return false;
    }
  }

  const std::wstring transactionSuffix =
      L"." + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
  for (std::size_t i = 0; i < finals.size(); ++i) {
    temps[i] = finals[i];
    temps[i] += L".tmp" + transactionSuffix;
    backups[i] = finals[i];
    backups[i] += L".bak" + transactionSuffix;
    std::ofstream output(temps[i], std::ios::binary | std::ios::trunc);
    if (!output || !output.write(contents[i]->data(), static_cast<std::streamsize>(contents[i]->size()))) {
      error = "Could not write temporary file for '" + pathToUtf8(finals[i]) + "'.";
      for (std::size_t j = 0; j <= i; ++j) std::filesystem::remove(temps[j], ec);
      return false;
    }
  }

  bool committed = true;
  for (std::size_t i = 0; i < finals.size(); ++i) {
    ec.clear();
    const bool exists = std::filesystem::exists(finals[i], ec);
    if (!ec && exists) {
      std::filesystem::rename(finals[i], backups[i], ec);
      backedUp[i] = !ec;
    }
    if (ec) {
      error = "Could not back up '" + pathToUtf8(finals[i]) + "': " + ec.message();
      committed = false;
      break;
    }
  }
  if (committed) {
    for (std::size_t i = 0; i < finals.size(); ++i) {
      ec.clear();
      std::filesystem::rename(temps[i], finals[i], ec);
      installed[i] = !ec;
      if (ec) {
        error = "Could not install '" + pathToUtf8(finals[i]) + "': " + ec.message();
        committed = false;
        break;
      }
    }
  }
  if (!committed) {
    for (std::size_t i = 0; i < finals.size(); ++i) {
      ec.clear();
      if (installed[i]) std::filesystem::remove(finals[i], ec);
      ec.clear();
      if (backedUp[i]) std::filesystem::rename(backups[i], finals[i], ec);
    }
  } else {
    for (const auto& backup : backups) std::filesystem::remove(backup, ec);
  }
  for (const auto& temp : temps) std::filesystem::remove(temp, ec);
  return committed;
}

}  // namespace editor
