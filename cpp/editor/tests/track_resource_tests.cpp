#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "EditorState.hpp"
#include "EditorTrackDefinition.hpp"
#include "Track.hpp"
#include "TrackResourceDocument.hpp"
#include "TrackResourceSave.hpp"

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void writeFile(const std::filesystem::path& path, const std::string& contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << contents;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: track_resource_tests <schema-10-track.json>\n";
    return 2;
  }
  const std::filesystem::path fixture = argv[1];
  editor::TrackDefinition authored = editor::fromFile(fixture);
  const tox::TrackLoadResult baked = tox::Track::fromFile(fixture);
  check(static_cast<bool>(baked), "fixture bakes");
  if (!baked) return 1;

  editor::EditorState offsetState(authored);
  int widthIndex = -1;
  for (int i = 0; i < static_cast<int>(offsetState.track().paths[0].points.size()); ++i)
    if (offsetState.track().paths[0].points[i].kind == editor::PointKind::Width) {
      widthIndex = i;
      break;
    }
  check(widthIndex >= 0, "fixture has a Width point for center-offset editing");
  if (widthIndex >= 0) {
    offsetState.selectPoint(0, widthIndex);
    offsetState.beginDrag();
    offsetState.dragSelectedWidthCenterOffsetTo(12.5);
    offsetState.dragSelectedWidthCenterOffsetTo(25.0);
    offsetState.endDrag();
    check(offsetState.track().paths[0].points[widthIndex].centerOffsetPercent == 25.0,
          "width center-offset slider updates the selected Width point");
    check(editor::toJson(offsetState.track()).find("centerOffsetPercent") != std::string::npos,
          "non-zero width center offset serializes");
    check(offsetState.undo() && offsetState.track().paths[0].points[widthIndex].centerOffsetPercent == 0.0,
          "one width center-offset slider gesture creates one undo step");
    check(editor::toJson(offsetState.track()).find("centerOffsetPercent") == std::string::npos,
          "zero width center offset is omitted from JSON");
  }

  const std::filesystem::path temp =
      std::filesystem::temp_directory_path() / "tungsten-oxide-track-resource-tests";
  std::error_code ec;
  std::filesystem::remove_all(temp, ec);
  std::filesystem::create_directories(temp);
  const std::filesystem::path resources = temp / "Resources.xml";

  check(editor::isSafeResourceRelativePath("tracks/MyTrack.json"), "nested resource-relative path is safe");
  check(!editor::isSafeResourceRelativePath("../MyTrack.json") &&
            !editor::isSafeResourceRelativePath("C:/tracks/MyTrack.json"),
        "traversing and absolute resource paths are rejected");

  editor::TrackSavePlan first =
      editor::prepareTrackSave(authored, *baked.track, {}, resources, std::nullopt, true);
  check(first.ok(), "first Save As plan is valid: " + first.error);
  check(!first.requiresConfirmation(), "new destination needs no overwrite confirmation");
  check(first.resultingBinding.resourceName == authored.name, "first save captures exact metadata name as resource identity");
  std::string commitError;
  check(editor::commitTrackSave(first, commitError), "first three-file transaction commits: " + commitError);
  check(std::filesystem::exists(first.jsonPath) && std::filesystem::exists(first.modelPath) &&
            std::filesystem::exists(first.xmlPath),
        "transaction writes XML, JSON and model");

  const editor::TrackResourceScanResult scan = editor::scanTrackResources(resources);
  check(scan.validDocument() && scan.tracks.size() == 1 && scan.tracks[0].loadable(),
        "saved Resources XML discovers one loadable Track");
  check(scan.tracks[0].resourceName == authored.name, "chooser exposes Resource@name");

  // <Models> list (TRACK_MODEL_LIST_PLAN.md Milestone 5): exactly one primary Track-type Model, its
  // own id present, and a <Meshes> entry (Type=Track) for every baked collidable batch.
  check(scan.tracks[0].models.size() == 1, "a freshly saved Track resource embeds exactly one Model");
  std::string primaryModelId;
  if (scan.tracks[0].models.size() == 1) {
    const modelxml::ModelXmlDefinition& primary = scan.tracks[0].models[scan.tracks[0].primaryModelIndex];
    check(primary.id.has_value() && !primary.id->empty(), "the primary Model has a non-empty id");
    if (primary.id.has_value()) primaryModelId = *primary.id;
    check(primary.trackData.has_value() && *primary.trackData == scan.tracks[0].trackDataReference,
          "the primary Model's TrackData matches trackDataReference");
    check(!primary.meshes.empty() && std::all_of(primary.meshes.begin(), primary.meshes.end(),
                                                 [](const modelxml::MeshMetadataXmlDefinition& mesh) { return mesh.type == modelxml::MeshType::Track; }),
          "every mesh in the primary Model is Type=Track");
  }

  const std::string stableIdentity = first.resultingBinding.resourceName;
  authored.name = "Changed JSON Metadata Name";
  const tox::TrackLoadResult changedBake = tox::Track::fromJson(editor::toJson(authored));
  check(static_cast<bool>(changedBake), "metadata-renamed track still bakes");
  editor::TrackSavePlan changed = editor::prepareTrackSave(
      authored, *changedBake.track, {}, resources, first.resultingBinding, false);
  check(changed.ok(), "bound Save plan is valid after metadata rename: " + changed.error);
  check(changed.resultingBinding.resourceName == stableIdentity,
        "bound Resource@name remains stable after metadata rename");
  check(changed.json.find("Changed JSON Metadata Name") != std::string::npos,
        "changed metadata name is written to JSON");

  writeFile(first.jsonPath, readFile(first.jsonPath) + " ");
  const editor::TrackSavePlan conflict = editor::prepareTrackSave(
      authored, *changedBake.track, {}, resources, first.resultingBinding, false);
  check(conflict.errorKind == editor::TrackSaveErrorKind::ExternalJsonConflict,
        "bound Save detects external JSON edits");
  writeFile(first.jsonPath, first.resultingBinding.jsonFingerprint);

  const std::filesystem::path copyXml = temp / "copy" / "Resources.xml";
  const editor::TrackSavePlan copy = editor::prepareTrackSave(
      authored, *changedBake.track, {}, copyXml, first.resultingBinding, true);
  check(copy.ok() && copy.resultingBinding.resourceName == stableIdentity,
        "Save As copies stable resource identity to a different XML");
  check(copy.jsonPath.parent_path() == copyXml.parent_path() &&
            copy.modelPath.parent_path() == copyXml.parent_path(),
        "Save As creates sidecars beside target XML");

  const std::string base =
      "<?xml version=\"1.0\"?><Resources><!--keep--><Resource type=\"Image\" name=\"X\"/>"
      "<Namespace name=\"Tracks\"><Resource type=\"Track\" name=\"Old\"/></Namespace></Resources>";
  const std::string generated =
      "<?xml version=\"1.0\"?><Resources><Namespace name=\"Tracks\"><Resource type=\"Track\" name=\"Old\">"
      "<Definitions><Definition factory=\"Track\"><TrackData>Old.json</TrackData></Definition></Definitions>"
      "</Resource></Namespace></Resources>";
  const editor::TrackResourceUpsertResult upsert = editor::upsertTrackResource(base, generated, "Old");
  check(upsert.error.empty() && upsert.replacedExisting, "upsert replaces complete matching Track resource");
  check(upsert.xml.find("<!--keep-->") != std::string::npos &&
            upsert.xml.find("type=\"Image\"") != std::string::npos,
        "upsert preserves unrelated comments and resources");

  const std::string collidingType =
      "<Resources><Namespace name=\"Tracks\"><Resource type=\"Image\" name=\"Old\"/>"
      "</Namespace></Resources>";
  check(!editor::upsertTrackResource(collidingType, generated, "Old").error.empty(),
        "same-name non-Track resource blocks save");

  const std::filesystem::path malformed = temp / "malformed.xml";
  writeFile(malformed, "<NotResources/>");
  const editor::TrackSavePlan malformedPlan =
      editor::prepareTrackSave(authored, *changedBake.track, {}, malformed, std::nullopt, true);
  check(!malformedPlan.ok() && readFile(malformed) == "<NotResources/>",
        "invalid existing XML is rejected and left untouched");

  // A zero-path TrackDefinition already fails to bake at all ("a current-schema track needs at
  // least one path" -- TrackLoader.cpp), so that particular no-geometry case never reaches
  // prepareTrackSave's own hasCollidableGeometry() check. That check exists as a second,
  // independent guard for tracks that *do* bake (at least one path/mesh region present) but end up
  // producing zero PathSurface/MeshSurface/ReservationWall/PathRail/MeshRail batches; exercise it
  // directly against a hand-built Track rather than trying to author a degenerate-but-bakeable
  // TrackDefinition.
  tox::Track geometryFreeTrack;
  const std::filesystem::path emptyXml = temp / "empty" / "Resources.xml";
  const editor::TrackSavePlan emptyPlan =
      editor::prepareTrackSave(authored, geometryFreeTrack, {}, emptyXml, std::nullopt, true);
  check(!emptyPlan.ok(), "a baked Track with no drivable/collidable geometry is refused at export time");
  check(!std::filesystem::exists(emptyXml), "refused export leaves no partial Resources XML behind");

  // A hand-injected Physical <Model> entry (simulating Milestone 6's "Load Model") must survive an
  // ordinary bound Save byte-for-byte -- Save regenerates the primary entry fresh, but writes back
  // whatever the in-session TrackDefinition's own `models` list holds for everything else
  // (TrackResourceSave.cpp sources `otherModels` from `track.models`, not from disk -- the editor is
  // the authoritative live source once Milestone 6 exists). parseCandidate() seeds `track->models`
  // with every non-primary entry at load time (Milestone 6), so round-tripping through a real
  // load (scanTrackResources) is what's exercised here, not just an in-memory field poke.
  {
    std::string xmlWithProp = readFile(resources);
    const std::string propModel =
        "<Model id=\"cube-model\"><ModelFile>Cube.mppmodel</ModelFile><Meshes>"
        "<Mesh><Name>main</Name><Type>Physical</Type><Visible>true</Visible></Mesh>"
        "</Meshes></Model>";
    const std::size_t pos = xmlWithProp.find("</Models>");
    check(pos != std::string::npos, "generated XML contains a </Models> closing tag to inject before");
    if (pos != std::string::npos) {
      xmlWithProp.insert(pos, propModel);
      writeFile(resources, xmlWithProp);

      const editor::TrackResourceScanResult withPropScan = editor::scanTrackResources(resources);
      check(withPropScan.validDocument() && withPropScan.tracks.size() == 1 && withPropScan.tracks[0].models.size() == 2,
            "the injected Physical Model is discovered alongside the primary");
      check(withPropScan.tracks.size() == 1 && withPropScan.tracks[0].track.has_value() &&
                withPropScan.tracks[0].track->models.size() == 1 && withPropScan.tracks[0].track->models[0].id == "cube-model",
            "loading seeds the editable TrackDefinition's own models list with the non-primary entry");

      // Hand-editing `resources` above is exactly the kind of external change the bound-Save
      // fingerprint guard exists to catch (it just did, correctly, before this fix) -- so this
      // resave must bind fresh off the just-rescanned file, mirroring how main.cpp's own load path
      // populates a TrackSaveBinding from a freshly scanned TrackResourceCandidate, not carry
      // forward `first.resultingBinding` from before the external edit.
      const editor::TrackResourceCandidate& withProp = withPropScan.tracks[0];
      const editor::TrackSaveBinding freshBinding{resources,          withProp.resourceName,       withProp.trackDataReference,
                                                  withProp.modelFileReference, withProp.resourceFingerprint, withProp.jsonFingerprint};
      const tox::TrackLoadResult withPropBake = tox::Track::fromJson(editor::toJson(*withProp.track));
      check(static_cast<bool>(withPropBake), "the freshly loaded-with-prop track still bakes: " + withPropBake.error);
      editor::TrackSavePlan resaved = editor::prepareTrackSave(*withProp.track, *withPropBake.track, {}, resources, freshBinding, false);
      check(resaved.ok(), "bound Save succeeds with a Physical Model present: " + resaved.error);
      std::string commitError2;
      check(editor::commitTrackSave(resaved, commitError2), "resave transaction commits: " + commitError2);

      const editor::TrackResourceScanResult afterResave = editor::scanTrackResources(resources);
      check(afterResave.validDocument() && afterResave.tracks.size() == 1 && afterResave.tracks[0].models.size() == 2,
            "the Physical Model survives a bound Save sourced from the in-session models list");
      if (afterResave.tracks.size() == 1) {
        const auto& modelsAfter = afterResave.tracks[0].models;
        const bool cubeStillPresent = std::any_of(modelsAfter.begin(), modelsAfter.end(), [](const modelxml::ModelXmlDefinition& m) {
          return m.id.has_value() && *m.id == "cube-model" && m.modelFile == "Cube.mppmodel";
        });
        check(cubeStillPresent, "the Physical Model's own id/modelFile are unchanged after resave");
        check(!primaryModelId.empty() && afterResave.tracks[0].models[afterResave.tracks[0].primaryModelIndex].id == primaryModelId,
              "the primary Model's own id is stable across saves");
      }
    }
  }

  // Hard break, no migration (TRACK_MODEL_LIST_PLAN.md): a Track resource using the old bare
  // <TrackData>/<ModelFile> shape (no <Models> wrapper) must fail to load with a clear error.
  {
    const std::filesystem::path oldShapeXml = temp / "old-shape" / "Resources.xml";
    std::filesystem::create_directories(oldShapeXml.parent_path());
    writeFile(oldShapeXml,
              "<?xml version=\"1.0\"?><Resources><Namespace name=\"Tracks\"><Resource type=\"Track\" name=\"Old\">"
              "<Definitions><Definition factory=\"Track\"><TrackData>Old.json</TrackData>"
              "<ModelFile>Old.mppmodel</ModelFile></Definition></Definitions></Resource></Namespace></Resources>");
    const editor::TrackResourceScanResult oldShapeScan = editor::scanTrackResources(oldShapeXml);
    check(oldShapeScan.validDocument() && oldShapeScan.tracks.size() == 1 && !oldShapeScan.tracks[0].loadable(),
          "old bare-shape Track resource (no <Models>) fails to load");
    check(oldShapeScan.tracks.size() == 1 && oldShapeScan.tracks[0].error.find("Models") != std::string::npos,
          "the old-shape error names <Models>: " + (oldShapeScan.tracks.empty() ? "" : oldShapeScan.tracks[0].error));
  }

  std::filesystem::remove_all(temp, ec);
  if (failures == 0) std::cout << "PASS: Resources XML Track save/load transaction\n";
  return failures == 0 ? 0 : 1;
}
