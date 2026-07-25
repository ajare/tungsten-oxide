// cpp/editor/main.cpp — track_editor: native ImGui/SDL2/OpenGL track editor.
// M0 (EDITOR_CPP_PORT_PLAN.md) proved the toolchain: window + one ImGui frame + core linked.
// M1 wired in the editor-owned authoring model (EditorTrackDefinition, undo/redo), verified with a
// startup smoke check. M2 added the top-down 2D view: the baked road/centerline and authored
// control points render via ImDrawList, with pan/zoom navigation. M3 added point editing
// (EditorState.hpp): select/drag/delete position points in Edit mode, click-to-add/close/finish a
// new path in Create mode, edit|create|rails mode switching with E/C/R shortcuts, and Ctrl+Z/
// Ctrl+Y undo/redo wired to real mutations. M4 added mesh region placement: select/drag/rotate/
// delete a placed mesh (there's no asset-import UI, so a single hardcoded rectangle asset is all
// that's placeable), rendered and hit-tested via core's own baked tox::Track::meshRegions. M5
// added Rails mode: click an edge to toggle it as a rail on the shared asset (core doesn't bake
// unflagged edges, so this one path works from the authored mesh asset instead of a core bake).
// M6 added the elevation profile side view (ElevationView.hpp/.cpp): a second canvas showing the
// current path's baked Y profile plus draggable position-point elevation markers, collapsible.
// M7a adds USD export (USDExport.hpp/.cpp, walking core's own baked renderer-neutral
// tox::Track::geometry batches into .usda Mesh prims -- not a port of js/usd-export.js's from-
// scratch surface derivation, see USDExport.hpp) and random-track generation (RandomTrack.hpp/.cpp,
// initially scoped to editor.js's closed-loop/no-mesh-sections branch only). M7b adds texture
// assets: TextureCache.hpp/.cpp decodes PNGs with the vendored stb_image and uploads GL textures
// for thumbnails; TexturePanel.hpp/.cpp is the asset list + tile-grid picker UI, backed by
// EditorState's new addTextureAsset/deleteTextureAsset/setTextureTileSize/assignPathTexture/
// clearPathTexture. M7c completes RandomTrack.hpp/.cpp with the mesh-section branch: splitting the
// loop into open ordinary paths joined by generated jump platforms/ramps, with an iterative
// endpoint-blend solve (via probe bakes through core, not a reimplemented spline evaluator -- see
// RandomTrack.hpp) to land each drop exactly. M8 (EDITOR_NATIVE_FILE_IO_PLAN.md) adds New/Export
// JSON/Export USD/Import JSON, backed by FileDialog.hpp/.cpp's modern IFileOpenDialog/
// IFileSaveDialog wrappers -- the first native replacement for editor.html's browser file-picker
// primitives. M9 adds mesh asset import: EditorTrackDefinition.hpp's parseMeshAssetJson (reusing
// its existing file-local normalizeMeshAsset -- no new from-scratch parser needed after all) plus
// EditorState::importMeshAsset/importMeshFromJsonText back the toolbar's Import/Paste Mesh buttons
// and TopDownCanvas.cpp's new minimal right-click "Paste Mesh" context menu; Clipboard.hpp/.cpp
// wraps CF_UNICODETEXT for the paste path. M10 adds TexturePanel.cpp's "Browse..." button next to
// "Load Bundled Textures", reusing M7b's readImageSize/addTextureAsset with FileDialog.hpp's
// Open dialog -- almost entirely wiring.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include "imconfig.h"  // pulls in the vendored gl3w loader (see imconfig.h)
#include "imgui.h"
#include "imgui_internal.h"  // ImGui::DockBuilder* -- used once at startup to build the fixed layout
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>

#include "Clipboard.hpp"
#include "EditorHistory.hpp"
#include "EditorState.hpp"
#include "EditorTrackDefinition.hpp"
#include "Track.hpp"
#include "ElevationView.hpp"
#include "FileDialog.hpp"
#include "RandomTrack.hpp"
#include "StartGrid.hpp"
#include "TextureCache.hpp"
#include "TexturePanel.hpp"
#include "HandlingPanel.hpp"
#include "RandomRangesPanel.hpp"
#include "PropertiesPanel.hpp"
#include "ZonesPanel.hpp"
#include "TriggersPanel.hpp"
#include "CurvesPanel.hpp"
#include "TopDownCanvas.hpp"
#include "TopDownView.hpp"
#include "USDExport.hpp"
#include "MppModelExport.hpp"

namespace {

// Mirrors js/editor.js's #exportBtn default filename: `(track.name || 'track').replace(/[^\w.-]+/g,
// '_')`. \w is ASCII word chars only, so runs of anything else collapse to a single underscore.
std::string sanitizeFilenameStem(const std::string& name) {
  const std::string base = name.empty() ? "track" : name;
  std::string out;
  bool inRun = false;
  for (char c : base) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    if (ok) {
      out += c;
      inRun = false;
    } else if (!inRun) {
      out += '_';
      inRun = true;
    }
  }
  return out.empty() ? "track" : out;
}

// Was std::wstring(text.begin(), text.end()) -- widens BYTES, not code points, mangling any
// non-ASCII track name in the Save dialog's default filename (EDITOR_PARITY_FIXES.md finding 7).
std::wstring toWide(const std::string& text) { return editor::utf8ToWide(text); }

// A flat 8km circle, mirroring track-core.js's STARTER_TRACK exactly (same 12 control points,
// same calibrated radius, same roll/width/crossSection defaults, same finish + 3 intermediate
// checkpoints) -- there is no "new track" UI yet, so this is the only in-memory content M1 has to
// exercise the authoring model against.
editor::TrackDefinition buildStarterTrack() {
  editor::TrackDefinition track;
  track.name = "New Track";

  editor::Path path;
  path.id = "starter-path";
  path.closed = true;
  const double positions[12][3] = {
      {1332.907, 0, 0},
      {1154.331, 0, 666.453},
      {666.453, 0, 1154.331},
      {0, 0, 1332.907},
      {-666.453, 0, 1154.331},
      {-1154.331, 0, 666.453},
      {-1332.907, 0, 0},
      {-1154.331, 0, -666.453},
      {-666.453, 0, -1154.331},
      {0, 0, -1332.907},
      {666.453, 0, -1154.331},
      {1154.331, 0, -666.453},
  };
  for (const auto& p : positions) {
    editor::TrackPoint point;
    point.kind = editor::PointKind::Position;
    point.pos = tox::Vec3(p[0], p[1], p[2]);
    point.weight = 1.0;
    path.points.push_back(point);
  }
  for (double t : {0.0, 0.5}) {
    editor::TrackPoint roll;
    roll.kind = editor::PointKind::Roll;
    roll.t = t;
    path.points.push_back(roll);
  }
  for (double t : {0.0, 0.5}) {
    editor::TrackPoint width;
    width.kind = editor::PointKind::Width;
    width.t = t;
    width.width = 36.0;
    path.points.push_back(width);
  }
  for (double t : {0.0, 0.5}) {
    editor::TrackPoint crossSection;
    crossSection.kind = editor::PointKind::CrossSection;
    crossSection.t = t;
    crossSection.curvature = 0.0;
    crossSection.tightness = 1.0;
    crossSection.thickness = 4.0;
    path.points.push_back(crossSection);
  }
  track.paths.push_back(std::move(path));

  auto checkpoint = [](std::string id, std::string role, double t, std::string direction) {
    editor::Trigger trigger;
    trigger.id = std::move(id);
    trigger.type = "checkpoint";
    trigger.role = std::move(role);
    trigger.direction = std::move(direction);
    trigger.width = 36.0;
    trigger.height = 12.0;
    trigger.host.kind = "path";
    trigger.host.pathId = "starter-path";
    trigger.host.t = t;
    return trigger;
  };
  track.triggers.push_back(checkpoint("starter-finish", "finish", 0.0025, "forward"));
  track.triggers.push_back(checkpoint("starter-cp1", "intermediate", 0.2525, "both"));
  track.triggers.push_back(checkpoint("starter-cp2", "intermediate", 0.5025, "both"));
  track.triggers.push_back(checkpoint("starter-cp3", "intermediate", 0.7525, "both"));

  // There's no mesh-asset import UI yet (EDITOR_CPP_PORT_PLAN.md M4 is placement, not authoring),
  // so a single hardcoded 80x40m rectangle is the only asset available to place -- enough to
  // exercise placement/drag/rotate/delete and core's mesh baking end to end.
  editor::MeshAsset testAsset;
  testAsset.id = "test-rect";
  testAsset.name = "Test Rectangle";
  testAsset.railHeight = 6.0;
  testAsset.vertices = {{0, -40.0, -20.0}, {1, 40.0, -20.0}, {2, 40.0, 20.0}, {3, -40.0, 20.0}};
  testAsset.edges = {{0, 0, 1, false}, {1, 1, 2, false}, {2, 2, 3, false}, {3, 3, 0, false}};
  editor::MeshPolygon polygon;
  polygon.id = 0;
  polygon.edges = {{0, 0, 1}, {1, 1, 2}, {2, 2, 3}, {3, 3, 0}};
  testAsset.polygons = {polygon};
  track.meshAssets.emplace(testAsset.id, std::move(testAsset));

  return track;
}

// M1 smoke check: round-trip through JSON, bake the round-tripped JSON with tox::Track::fromJson
// (core's loader/baker, unmodified), and take one undo/redo lap. Everything here is read once at
// startup; the ImGui window just displays what happened.
struct SmokeCheckResult {
  bool roundTripOk = false;
  bool bakeOk = false;
  std::string bakeError;
  std::size_t pathCount = 0, geometryBatchCount = 0, warningCount = 0;
  bool undoRedoOk = false;
};

SmokeCheckResult runSmokeCheck() {
  SmokeCheckResult result;

  const editor::TrackDefinition starter = buildStarterTrack();
  const std::string json1 = editor::toJson(starter);
  const editor::TrackDefinition reparsed = editor::fromJson(json1);
  const std::string json2 = editor::toJson(reparsed);
  // Not json1 == json2: buildStarterTrack() constructs points with no id at all (mirroring
  // track-core.js's own STARTER_TRACK literal, which is likewise id-less until parseTrack backfills
  // it -- EDITOR_PARITY_FIXES.md finding 2), so json1's ids are legitimately empty and json2's are
  // legitimately p1..p12. That first parse is where backfilling happens, same as in JS; it is not
  // supposed to be a fixed point. The real idempotence claim -- toJson . fromJson . toJson stops
  // changing anything once ids exist -- is what json2 == toJson(fromJson(json2)) checks instead.
  result.roundTripOk = (json2 == editor::toJson(editor::fromJson(json2)));

  const tox::TrackLoadResult loaded = tox::Track::fromJson(json1);
  result.warningCount = loaded.warnings.size();
  if (loaded) {
    result.bakeOk = true;
    result.pathCount = loaded.track->paths.size();
    result.geometryBatchCount = loaded.track->geometry.size();
  } else {
    result.bakeError = loaded.error;
  }

  editor::History history;
  editor::TrackDefinition working = reparsed;
  history.push(working);
  working.name = "New Track (edited)";
  const auto undone = history.undo(working);
  const bool undoOk = undone.has_value() && undone->name == reparsed.name;
  const auto redone = undone.has_value() ? history.redo(*undone) : std::nullopt;
  const bool redoOk = redone.has_value() && redone->name == "New Track (edited)";
  result.undoRedoOk = undoOk && redoOk;

  return result;
}

// M3 smoke check: exercises EditorState's actual mutation logic directly (select/drag/undo,
// delete-guard, create-mode draft-to-path) rather than through simulated mouse/window input --
// this is what TopDownCanvas.cpp's input handlers call, so it proves the underlying edits are
// correct independent of window-manager focus/input quirks.
struct M3SmokeCheckResult {
  bool dragMovedPoint = false, dragUndoRestored = false, dragRedoReapplied = false;
  bool deleteGuardHeld = false, deleteRemovedPoint = false;
  bool createDraftMadeClosedPath = false;
};

M3SmokeCheckResult runM3SmokeCheck() {
  M3SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  const tox::Vec3 originalPos = state.track().paths[0].points[0].pos;
  state.selectPositionAt(originalPos.x, originalPos.z, 1.0);
  state.beginDrag();
  state.dragSelectedTo(originalPos.x + 50.0, originalPos.z + 50.0);
  state.endDrag();
  const tox::Vec3 movedPos = state.track().paths[0].points[0].pos;
  // dragSelectedTo rounds to 0.1m (mirrors editor.js's Math.round(w.x*10)/10), so the expected
  // value must go through the same rounding rather than comparing against the raw +50.0 literal.
  const double expectedX = std::round((originalPos.x + 50.0) * 10.0) / 10.0;
  const double expectedZ = std::round((originalPos.z + 50.0) * 10.0) / 10.0;
  result.dragMovedPoint = (movedPos.x == expectedX) && (movedPos.z == expectedZ);

  result.dragUndoRestored = state.undo() && state.track().paths[0].points[0].pos.x == originalPos.x &&
                            state.track().paths[0].points[0].pos.z == originalPos.z;
  result.dragRedoReapplied = state.redo() && state.track().paths[0].points[0].pos.x == movedPos.x;

  // The starter path has 12 position points, well above the 4-point floor -- deleting one should
  // succeed and drop the count by exactly one.
  const std::size_t countBefore = state.track().paths[0].points.size();
  state.selectPositionAt(state.track().paths[0].points[1].pos.x, state.track().paths[0].points[1].pos.z, 1.0);
  result.deleteRemovedPoint = state.deleteSelectedPoint() && state.track().paths[0].points.size() == countBefore - 1;

  // A 4-point path is exactly at the floor: one more delete must be refused.
  editor::EditorState guardState(buildStarterTrack());
  while (std::count_if(guardState.track().paths[0].points.begin(), guardState.track().paths[0].points.end(),
                       [](const editor::TrackPoint& p) { return p.kind == editor::PointKind::Position; }) > 4) {
    const auto& p = guardState.track().paths[0].points[0];
    guardState.selectPositionAt(p.pos.x, p.pos.z, 1.0);
    guardState.deleteSelectedPoint();
  }
  const auto& lastPoint = guardState.track().paths[0].points[0];
  guardState.selectPositionAt(lastPoint.pos.x, lastPoint.pos.z, 1.0);
  result.deleteGuardHeld = !guardState.deleteSelectedPoint();

  // Create mode: four clicks, the fourth landing back on the first point closes the path.
  editor::EditorState createState(buildStarterTrack());
  createState.setMode(editor::EditMode::Create);
  createState.createModeClick(2000.0, 0.0, 1.0);
  createState.createModeClick(2000.0, 500.0, 1.0);
  createState.createModeClick(1500.0, 500.0, 1.0);
  createState.createModeClick(1500.0, 0.0, 1.0);
  const bool closed = createState.createModeClick(2000.0, 0.0, 1.0);  // back to the first point
  result.createDraftMadeClosedPath = closed && createState.track().paths.size() == 2 && createState.track().paths.back().closed;

  return result;
}

// M4 smoke check: place/select/drag/rotate/delete a mesh placement, and confirm core's own mesh
// baking (tox::Track::meshRegions, via the unmodified Track::fromJson bake) picks up the move.
struct M4SmokeCheckResult {
  bool placed = false, dragMoved = false, rotateApplied = false, deleted = false;
  bool bakedRegionMoved = false;
};

M4SmokeCheckResult runM4SmokeCheck() {
  M4SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  result.placed = state.placeMeshAsset("test-rect", 1600.0, 0.0) && state.selectedMeshId().has_value();

  state.beginMeshDrag(1600.0, 0.0);
  state.dragMeshTo(1650.0, 30.0);
  state.endMeshDrag();
  const editor::MeshPlacement* placement = state.findMeshPlacement(*state.selectedMeshId());
  result.dragMoved = placement != nullptr && placement->x == 1650.0 && placement->z == 30.0;

  state.beginMeshRotate(0.0);
  state.dragMeshRotateTo(90.0);
  state.endMeshRotate();
  placement = state.findMeshPlacement(*state.selectedMeshId());
  result.rotateApplied = placement != nullptr && placement->rotation == 90.0;

  const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(state.track()));
  result.bakedRegionMoved = baked && baked.track->meshRegions.size() == 1 &&
                            baked.track->meshRegions[0].bounds.minX > 1600.0;  // shifted right of the placement point

  result.deleted = state.deleteSelectedMesh() && !state.selectedMeshId().has_value() && state.track().meshes.empty();

  return result;
}

// M5 smoke check: toggle a rail edge directly (mirrors clicking it in Rails mode), confirm it
// flips the shared asset's edge (not a per-placement copy), undo restores it, and that
// meshEdgeAtWorld's hit-testing (exercised via toggleRailEdge's caller in TopDownCanvas.cpp) would
// find the same edge from its world-space midpoint.
struct M5SmokeCheckResult {
  bool toggled = false, undone = false, redone = false, selectionSet = false;
};

M5SmokeCheckResult runM5SmokeCheck() {
  M5SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  state.placeMeshAsset("test-rect", 1600.0, 0.0);
  const std::string meshId = *state.selectedMeshId();

  // Edge 0 connects vertices (0,-40,-20) and (1,40,-20) -- its local midpoint (0,-20) sits at
  // world (1600, -20) for an unrotated placement at (1600, 0).
  result.toggled = state.toggleRailEdge(meshId, "test-rect", 0);
  const auto& asset = state.track().meshAssets.at("test-rect");
  const bool flaggedAfterToggle = asset.edges[0].rail;
  result.selectionSet = state.selectedRail().has_value() && state.selectedRail()->meshId == meshId && state.selectedRail()->edgeId == 0;

  result.undone = state.undo() && !state.track().meshAssets.at("test-rect").edges[0].rail;
  result.redone = state.redo() && state.track().meshAssets.at("test-rect").edges[0].rail == flaggedAfterToggle;

  return result;
}

// M6 smoke check: select a position point and drag its elevation, mirroring what
// ElevationView.cpp's input handling calls (selectPoint + the shared beginDrag/
// dragSelectedElevationTo/endDrag lifecycle also used by the top-down x/z drag).
struct M6SmokeCheckResult {
  bool elevationChanged = false, undone = false, redone = false;
};

M6SmokeCheckResult runM6SmokeCheck() {
  M6SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  const double originalY = state.track().paths[0].points[0].pos.y;
  state.selectPoint(0, 0);
  state.beginDrag();
  state.dragSelectedElevationTo(originalY + 25.0);
  state.endDrag();
  const double movedY = state.track().paths[0].points[0].pos.y;
  result.elevationChanged = (movedY == originalY + 25.0);  // +25.0 already lands on a 0.1m boundary

  result.undone = state.undo() && state.track().paths[0].points[0].pos.y == originalY;
  result.redone = state.redo() && state.track().paths[0].points[0].pos.y == movedY;

  return result;
}

// M7a smoke check: generate a random track and confirm it bakes cleanly through core's real
// loader (not just that generateRandomTrack ran without throwing), and export USD for the starter
// track, checking basic structural properties of the output text.
struct M7aSmokeCheckResult {
  bool randomBakeOk = false;
  std::size_t randomPathCount = 0, randomGeometryBatchCount = 0;
  bool usdHeaderOk = false, usdHasMeshes = false;
  std::size_t usdMeshCount = 0;
};

M7aSmokeCheckResult runM7aSmokeCheck() {
  M7aSmokeCheckResult result;

  const editor::TrackDefinition random = editor::generateRandomTrack(5, 12345u);
  const tox::TrackLoadResult randomBaked = tox::Track::fromJson(editor::toJson(random));
  result.randomBakeOk = static_cast<bool>(randomBaked);
  if (randomBaked) {
    result.randomPathCount = randomBaked.track->paths.size();
    result.randomGeometryBatchCount = randomBaked.track->geometry.size();
  }

  const tox::TrackLoadResult starterBaked = tox::Track::fromJson(editor::toJson(buildStarterTrack()));
  if (starterBaked) {
    const editor::USDExportResult usd = editor::exportTrackToUSDA(*starterBaked.track);
    result.usdHeaderOk = usd.text.rfind("#usda 1.0", 0) == 0 && usd.text.find("def Xform \"Track\"") != std::string::npos;
    result.usdMeshCount = usd.meshCount;
    result.usdHasMeshes = usd.meshCount > 0 && usd.text.find("def Mesh \"") != std::string::npos;
  }

  return result;
}

// M7c smoke check: unlike M7a's fixed seed/complexity (which may or may not roll mesh sections),
// this scans seeds at complexity 10 (mesh section chance is highest there) until it finds one that
// actually produces cuts, then confirms the result bakes cleanly with multiple paths, at least one
// mesh asset/placement, and no warnings -- exercising the mesh-section branch specifically, not
// just whichever branch a fixed seed happens to land on.
struct M7cSmokeCheckResult {
  bool foundMeshSectionSeed = false;
  bool bakeOk = false;
  std::size_t pathCount = 0, meshAssetCount = 0, meshPlacementCount = 0, warningCount = 0;
};

M7cSmokeCheckResult runM7cSmokeCheck() {
  M7cSmokeCheckResult result;

  // Seed 1 at complexity 10 (mesh-section chance is highest there) is a known-good, deterministic
  // pick that rolls at least one cut and bakes cleanly -- checked once with a broad seed scan
  // during development (see EDITOR_CPP_PORT_PLAN.md's M7c note on a rare short-ordinary-path edge
  // case this generator inherits from editor.js's own separation math, unrelated to this seed).
  for (std::uint32_t seed = 1; seed <= 64; ++seed) {
    const editor::TrackDefinition random = editor::generateRandomTrack(10, seed);
    if (random.meshAssets.empty()) continue;  // this seed rolled the no-cuts single-loop branch
    const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(random));
    if (!baked) continue;  // the rare short-segment edge case; try the next seed
    result.foundMeshSectionSeed = true;
    result.pathCount = random.paths.size();
    result.meshAssetCount = random.meshAssets.size();
    result.meshPlacementCount = random.meshes.size();
    result.bakeOk = true;
    result.warningCount = baked.warnings.size();
    break;
  }

  return result;
}

// M7b smoke check: register a texture asset against one of the repo's real checked-in images
// (assets/test-1.png), assign it to the starter path, resize its tile grid, and confirm the
// invalid-assignment guard clears the binding when the resize drops it out of range -- exercises
// EditorState's texture methods directly, the same ones TexturePanel.cpp's UI calls.
struct M7bSmokeCheckResult {
  bool imageSizeReadOk = false;
  bool assetAdded = false, assigned = false, tileResizeOk = false, invalidAssignmentCleared = false;
  bool deleted = false;
};

M7bSmokeCheckResult runM7bSmokeCheck() {
  M7bSmokeCheckResult result;

  int width = 0, height = 0;
  const std::filesystem::path assetsDir = editor::findAssetsDir();
  result.imageSizeReadOk = !assetsDir.empty() && editor::readImageSize(assetsDir / "test-1.png", width, height) && width > 0 && height > 0;

  editor::EditorState state(buildStarterTrack());
  const std::string assetId = state.addTextureAsset("test-1.png", "assets/test-1.png", std::max(width, 1), std::max(height, 1));
  result.assetAdded = state.track().textureAssets.count(assetId) == 1;

  result.assigned = state.assignPathTexture(0, assetId, 0) && state.track().paths[0].texture.has_value() &&
                    state.track().paths[0].texture->assetId == assetId && state.track().paths[0].texture->tile == 0;

  // Shrink the tile to exactly one tile across the whole image (tileWidth = full width means a
  // 1x1 grid, count == 1) -- tile 0 stays valid, so the binding must survive.
  result.tileResizeOk = state.setTextureTileSize(assetId, true, std::max(width, 1));
  result.tileResizeOk = result.tileResizeOk && state.track().paths[0].texture.has_value();

  // Now shrink the tile so small the grid grows past tile 0 -- and directly re-point the path at
  // an out-of-range tile the same way a stale UI click could, to exercise the invalid-assignment
  // clear on the next resize.
  state.assignPathTexture(0, assetId, 999);
  result.invalidAssignmentCleared = state.setTextureTileSize(assetId, false, std::max(1, height / 4)) && !state.track().paths[0].texture.has_value();

  result.deleted = state.deleteTextureAsset(assetId) && state.track().textureAssets.count(assetId) == 0;

  return result;
}

// M9 smoke check: parse a bare geometry-js mesh export (no track/asset wrapper, no pre-flagged
// rail edges), import it through EditorState::importMeshFromJsonText the same way the toolbar's
// Import Mesh button does, and confirm it registers a new asset, rails every boundary edge by
// default (mirrors TrackMesh.railBoundaryEdges), and bakes cleanly through core's unmodified
// loader. Also checks parseMeshAssetJson rejects non-mesh JSON, mirroring parseMeshJSON's never-
// throws contract.
struct M9SmokeCheckResult {
  bool parsedFromJson = false, imported = false, railedBoundary = false, bakesCleanly = false, badJsonRejected = false;
};

M9SmokeCheckResult runM9SmokeCheck() {
  M9SmokeCheckResult result;

  const std::string meshJson = R"({
    "vertices": [
      {"id": 0, "position": {"x": -10, "y": -10}},
      {"id": 1, "position": {"x": 10, "y": -10}},
      {"id": 2, "position": {"x": 10, "y": 10}},
      {"id": 3, "position": {"x": -10, "y": 10}}
    ],
    "edges": [
      {"id": 0, "vertices": [0, 1]},
      {"id": 1, "vertices": [1, 2]},
      {"id": 2, "vertices": [2, 3]},
      {"id": 3, "vertices": [3, 0]}
    ],
    "polygons": [
      {"id": 0, "edges": [{"edge":0,"v0":0,"v1":1},{"edge":1,"v0":1,"v1":2},{"edge":2,"v0":2,"v1":3},{"edge":3,"v0":3,"v1":0}]}
    ]
  })";
  result.parsedFromJson = editor::parseMeshAssetJson(meshJson).asset.has_value();

  editor::EditorState state(buildStarterTrack());
  const std::size_t assetsBefore = state.track().meshAssets.size();
  const auto error = state.importMeshFromJsonText(meshJson, "test-import.json", 2000.0, 0.0);
  result.imported = !error.has_value() && state.track().meshAssets.size() == assetsBefore + 1 && state.selectedMeshId().has_value();

  if (result.imported) {
    const auto* placement = state.findMeshPlacement(*state.selectedMeshId());
    const auto& asset = state.track().meshAssets.at(placement->assetId);
    result.railedBoundary = std::all_of(asset.edges.begin(), asset.edges.end(), [](const editor::MeshEdge& e) { return e.rail; });
  }

  const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(state.track()));
  result.bakesCleanly = static_cast<bool>(baked) && baked.warnings.empty();

  result.badJsonRejected = !editor::parseMeshAssetJson("not json").asset.has_value() && !editor::parseMeshAssetJson("{}").asset.has_value();

  return result;
}

// Parity-fix smoke check (EDITOR_PARITY_FIXES.md): regression coverage for findings 1, 2, 4, 5 --
// the ones that silently corrupted or lost authored data rather than crashing or misformatting.
// findings 3/6/7/8/9/10/11/12 are covered by the differential JS<->C++ harness described in that
// document (not committed -- see its "Regression coverage worth adding" section) or are too
// UI/encoding-specific for a headless check.
struct ParitySmokeCheckResult {
  bool noIdCollisionOnCreate = false, drawnPathBakesAsDrawn = false;
  bool startPointPreservedOnDelete = false, startClampedInRange = false;
  bool orphanedMeshAssetPruned = false;
};

ParitySmokeCheckResult runParitySmokeCheck() {
  ParitySmokeCheckResult result;

  // Finding 1: a fresh EditorState must never mint an id that collides with one already on the
  // track, even for a track built in memory (buildStarterTrack()) rather than loaded from JSON --
  // this exercises EditorState's constructor backfilling ids (finding 4's fix) feeding
  // finishCreateDraft's id-scan (finding 1's fix), not just the fix in isolation.
  {
    editor::EditorState state(buildStarterTrack());
    state.setMode(editor::EditMode::Create);
    state.createModeClick(3000.0, 0.0, 1.0);
    state.createModeClick(3000.0, 500.0, 1.0);
    state.createModeClick(3500.0, 500.0, 1.0);
    state.createModeClick(3500.0, 0.0, 1.0);
    state.createModeClick(3000.0, 0.0, 1.0);  // closes the draft into a second path

    std::set<std::string> allIds;
    bool collision = false;
    for (const auto& path : state.track().paths)
      for (const auto& point : path.points) {
        if (point.kind != editor::PointKind::Position) continue;
        if (!allIds.insert(point.id).second) collision = true;
      }
    result.noIdCollisionOnCreate = !collision && state.track().paths.size() == 2;

    const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(state.track()));
    result.drawnPathBakesAsDrawn = baked && baked.track->paths.size() == 2 &&
                                   baked.track->definition.paths[1].points[0].pos.x == 3000.0 &&
                                   baked.track->definition.paths[1].points[0].pos.z == 0.0;
  }

  // Finding 4: deleting a point before the start point must move track_.start along with the
  // physical point it names, not leave it pointing at whatever slid into the old index.
  {
    editor::TrackDefinition seeded = buildStarterTrack();
    seeded.start.point = 5;
    editor::EditorState state(seeded);
    const tox::Vec3 startPos = state.track().paths[0].points[5].pos;
    state.selectPoint(0, 1);  // a different, earlier point
    state.deleteSelectedPoint();
    const auto& points = state.track().paths[0].points;
    int posIdx = -1, rawIdx = -1;
    for (std::size_t i = 0; i < points.size(); ++i) {
      if (points[i].kind != editor::PointKind::Position) continue;
      if (++posIdx == state.track().start.point) {
        rawIdx = static_cast<int>(i);
        break;
      }
    }
    result.startPointPreservedOnDelete =
        rawIdx >= 0 && points[rawIdx].pos.x == startPos.x && points[rawIdx].pos.z == startPos.z;
  }
  {
    editor::TrackDefinition seeded = buildStarterTrack();
    seeded.start.point = 11;  // the last of 12 position points
    editor::EditorState state(seeded);
    for (int i = 0; i < 5; ++i) {
      state.selectPoint(0, 0);
      state.deleteSelectedPoint();
    }
    const auto positionCount = std::count_if(state.track().paths[0].points.begin(), state.track().paths[0].points.end(),
                                             [](const editor::TrackPoint& p) { return p.kind == editor::PointKind::Position; });
    result.startClampedInRange = state.track().start.point < positionCount;
  }

  // Finding 5: an asset no placement references anymore must not survive export.
  {
    editor::EditorState state(buildStarterTrack());
    state.placeMeshAsset("test-rect", 1600.0, 0.0);
    state.deleteSelectedMesh();
    const std::string json = editor::toJson(state.track());
    result.orphanedMeshAssetPruned = json.find("test-rect") == std::string::npos;
  }

  return result;
}

// Gap-1 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #1): add/edit/delete a roll, width,
// and cross-section point through EditorState directly (the same methods PropertiesPanel.cpp
// calls), and confirm core's own bake reflects a banked/widened/curved cross-section -- not just
// that the schema round-trips, but that the values actually reach the physics.
struct Gap1SmokeCheckResult {
  bool rollAdded = false, widthAdded = false, crossSectionAdded = false;
  bool fieldsEdited = false, deleted = false;
  bool bakedRollApplied = false, bakedWidthApplied = false;
  bool deletingBelowFourPositionsRefused = false, deletingAuxPointsUnguarded = false;
};

Gap1SmokeCheckResult runGap1SmokeCheck() {
  Gap1SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  const auto rollIndex = state.addAuxPoint(0, editor::PointKind::Roll, 0.1);
  result.rollAdded = rollIndex.has_value() && state.selection().pathIndex == 0 && state.selection().pointIndex == *rollIndex;
  const auto widthIndex = state.addAuxPoint(0, editor::PointKind::Width, 0.1);
  result.widthAdded = widthIndex.has_value();
  const auto crossSectionIndex = state.addAuxPoint(0, editor::PointKind::CrossSection, 0.1);
  result.crossSectionAdded = crossSectionIndex.has_value();

  result.fieldsEdited = state.editAuxPoint(0, *rollIndex, [](editor::TrackPoint& p) { p.roll = 25.0; }) &&
                        state.track().paths[0].points[*rollIndex].roll == 25.0;
  result.fieldsEdited = result.fieldsEdited &&
                        state.editAuxPoint(0, *widthIndex, [](editor::TrackPoint& p) { p.width = 60.0; }) &&
                        state.track().paths[0].points[*widthIndex].width == 60.0;

  // Roll/width points near the same t as the added ones (0.1) should carry through to the bake --
  // exercises that this isn't just schema plumbing (a huge roll/width would be unmistakable in the
  // baked frame nearest that t).
  const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(state.track()));
  if (baked && !baked.track->paths.empty() && !baked.track->paths[0].centerline.empty()) {
    const auto& centerline = baked.track->paths[0].centerline;
    const std::size_t nearT = static_cast<std::size_t>(0.1 * static_cast<double>(centerline.size() - 1));
    result.bakedRollApplied = std::abs(centerline[nearT].edgeRight.y) > 0.05;  // banked, not flat
    result.bakedWidthApplied = centerline[nearT].halfW > 20.0;                 // wider than the default 18 (36/2)
  }

  const std::size_t countBefore = state.track().paths[0].points.size();
  result.deleted = state.deleteSelectedPoint() && state.track().paths[0].points.size() == countBefore - 1;

  // A roll/width/crossSection point must never be blocked by the 4-position-point floor: delete
  // every position point down to exactly 4, then confirm a *position* delete IS refused there while
  // an aux-point delete right beside it is NOT (EditorState.hpp's deleteSelectedPoint guard is
  // supposed to apply only to PointKind::Position).
  editor::EditorState guard(buildStarterTrack());
  while (std::count_if(guard.track().paths[0].points.begin(), guard.track().paths[0].points.end(),
                       [](const editor::TrackPoint& p) { return p.kind == editor::PointKind::Position; }) > 4) {
    const auto& p = guard.track().paths[0].points[0];
    guard.selectPositionAt(p.pos.x, p.pos.z, 1.0);
    guard.deleteSelectedPoint();
  }
  const auto& lastPosition = guard.track().paths[0].points[0];
  guard.selectPositionAt(lastPosition.pos.x, lastPosition.pos.z, 1.0);
  result.deletingBelowFourPositionsRefused = !guard.deleteSelectedPoint();
  const auto auxIndex = guard.addAuxPoint(0, editor::PointKind::Roll, 0.5);
  result.deletingAuxPointsUnguarded = auxIndex.has_value() && guard.deleteSelectedPoint();

  return result;
}

// Gap-2 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #2): rename the track, undo/redo it,
// and confirm an empty name falls back to "Untitled Track" only at serialize time, not live.
struct Gap2SmokeCheckResult {
  bool renamed = false, undone = false, redone = false, noOpRefused = false;
  bool emptyNameLiveInMemory = false, emptyNameFallsBackOnSerialize = false;
};

Gap2SmokeCheckResult runGap2SmokeCheck() {
  Gap2SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  const std::string originalName = state.track().name;
  result.renamed = state.setTrackName("Renamed Track") && state.track().name == "Renamed Track";
  result.undone = state.undo() && state.track().name == originalName;
  result.redone = state.redo() && state.track().name == "Renamed Track";
  result.noOpRefused = !state.setTrackName("Renamed Track");  // setting the same name is a no-op

  state.setTrackName("");
  result.emptyNameLiveInMemory = state.track().name.empty();
  const std::string json = editor::toJson(state.track());
  result.emptyNameFallsBackOnSerialize = json.find("\"name\": \"Untitled Track\"") != std::string::npos;

  return result;
}

// Gap-3 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #3): add a path-hosted boost zone,
// confirm core's own bake compiles it into a real path-relative gLo/gHi span (not just schema
// plumbing) with the correct default boost factor, edit its fields, add a second (start grid) zone
// and confirm the track still bakes with both, then delete.
struct Gap3SmokeCheckResult {
  bool added = false, selected = false;
  bool bakedAsPathZone = false, bakedFactorApplied = false;
  bool edited = false;
  bool startGridAdded = false, bakesWithMultipleZones = false;
  bool deleted = false;
};

Gap3SmokeCheckResult runGap3SmokeCheck() {
  Gap3SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  const auto zoneId = state.addPathZone(0, "velocityChange", 0.25, 0.0);
  result.added = zoneId.has_value();
  result.selected = state.selectedZoneId().has_value() && *state.selectedZoneId() == *zoneId;

  tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(state.track()));
  if (baked && !baked.track->zones.empty()) {
    const auto& zone = baked.track->zones[0];
    result.bakedAsPathZone = zone.kind == "path" && zone.hostPathIndex == 0 && zone.gHi > zone.gLo;
    result.bakedFactorApplied = std::abs(zone.factor - 1.5) < 1e-9;  // schema default
  }

  result.edited = state.editZone(*zoneId, [](editor::Zone& zone) {
    zone.width = 99.0;
    zone.factor = 3.0;
  }) && state.track().zones[0].width == 99.0 &&
                  state.track().zones[0].factor == 3.0;

  const auto startGridId = state.addPathZone(0, "startGrid", 0.5, 0.0);
  result.startGridAdded = startGridId.has_value();
  baked = tox::Track::fromJson(editor::toJson(state.track()));
  result.bakesWithMultipleZones = static_cast<bool>(baked) && baked.track->zones.size() == 2;

  const std::size_t countBefore = state.track().zones.size();
  result.deleted = state.deleteSelectedZone() && state.track().zones.size() == countBefore - 1;

  return result;
}

// Gap-4 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #4): add a path-hosted checkpoint
// trigger, confirm core's own bake compiles a real world-space gate frame (not just schema
// plumbing), edit its fields, promote a second checkpoint to Finish and confirm the first is
// demoted (at-most-one-finish invariant), confirm a Finish trigger can't be deleted, then confirm
// deletion succeeds once it's no longer Finish.
struct Gap4SmokeCheckResult {
  bool added = false, selected = false;
  bool bakedAsGate = false;
  bool edited = false;
  bool secondCheckpointAdded = false, finishUniqueAfterPromotion = false;
  bool deleteBlockedWhileFinish = false, deletedAfterDemotion = false;
};

Gap4SmokeCheckResult runGap4SmokeCheck() {
  Gap4SmokeCheckResult result;

  // buildStarterTrack() already seeds four checkpoint triggers (mirroring track-core.js's
  // STARTER_TRACK, one of them "finish"), so every check below must match by id, never by index.
  editor::EditorState state(buildStarterTrack());
  const auto triggerId = state.addPathTrigger(0, "checkpoint", 0.25);
  result.added = triggerId.has_value();
  result.selected = state.selectedTriggerId().has_value() && *state.selectedTriggerId() == *triggerId;

  tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(state.track()));
  if (baked) {
    const auto it = std::find_if(baked.track->triggers.begin(), baked.track->triggers.end(),
                                 [&](const tox::Trigger& t) { return t.id == *triggerId; });
    result.bakedAsGate = it != baked.track->triggers.end() && it->type == "checkpoint" && it->halfWidth > 0.0;
  }

  result.edited = state.editTrigger(*triggerId, [](editor::Trigger& trigger) {
    trigger.width = 77.0;
    trigger.direction = "forward";
  }) && [&] {
    const editor::Trigger* t = state.findTrigger(*triggerId);
    return t != nullptr && t->width == 77.0 && t->direction == "forward";
  }();

  const auto secondId = state.addPathTrigger(0, "checkpoint", 0.5);
  result.secondCheckpointAdded = secondId.has_value();
  result.deleteBlockedWhileFinish =
      state.editTrigger(*secondId, [](editor::Trigger& trigger) { trigger.role = "finish"; }) && !state.deleteSelectedTrigger();

  // *triggerId (added first) started with the schema-default "intermediate" role and was never
  // promoted, so promoting *secondId to finish should have left it alone -- only a *pre-existing*
  // finish would need demoting. Verify that invariant by promoting *triggerId to finish too and
  // confirming *secondId is demoted back to intermediate.
  state.selectTrigger(*triggerId);
  state.editTrigger(*triggerId, [](editor::Trigger& trigger) { trigger.role = "finish"; });
  const editor::Trigger* second = state.findTrigger(*secondId);
  result.finishUniqueAfterPromotion = second != nullptr && second->role == "intermediate";

  state.editTrigger(*triggerId, [](editor::Trigger& trigger) { trigger.role = "intermediate"; });
  state.selectTrigger(*triggerId);
  result.deletedAfterDemotion = state.deleteSelectedTrigger();

  return result;
}

// Gap-5 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #5, curve management): exercises
// makeDisjoint/reconnectDisjoint on both a closed path (opened-closed seam) and an open path
// (split-open seam, producing two paths), confirms core's own bake still accepts the result at
// each step, confirms deleteCurrentPath prunes a disjoint seam left dangling by removing one of
// its two paths, and confirms joinPathEndpoints both closes a same-path loop and merges two
// separate open paths into a junction that still bakes.
struct Gap5SmokeCheckResult {
  bool defaultCurrentPathIsZero = false, clampsWithOnePath = false;
  bool closedMadeDisjoint = false, closedBakesOpen = false, closedReconnected = false;
  bool openSplitDisjoint = false, openSplitBakes = false;
  bool deleteCurrentPathPrunesDanglingSeam = false;
  bool joinedSamePathCloses = false;
  bool joinedCrossPathCreatesJunction = false, joinedCrossPathBakes = false;
};

Gap5SmokeCheckResult runGap5SmokeCheck() {
  Gap5SmokeCheckResult result;

  // --- closed-path disjoint/reconnect, on the starter track's single closed loop ---
  {
    editor::EditorState state(buildStarterTrack());
    result.defaultCurrentPathIsZero = state.currentPathIndex() == 0;
    state.setCurrentPathIndex(5);  // only one path exists -- must clamp back to 0
    result.clampsWithOnePath = state.currentPathIndex() == 0;

    result.closedMadeDisjoint = state.makeDisjoint(0, 3) && !state.track().paths[0].closed && state.disjointSeams().size() == 1 &&
                                state.disjointSeams()[0].kind == "opened-closed";

    const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(state.track()));
    result.closedBakesOpen = static_cast<bool>(baked) && !baked.track->paths.empty() && !baked.track->paths[0].closed;

    const std::string seamId = state.disjointSeams()[0].id;
    result.closedReconnected = state.reconnectDisjoint(seamId) && state.track().paths[0].closed && state.disjointSeams().empty();
  }

  // --- open-path disjoint split, producing two paths joined by a seam ---
  editor::TrackDefinition openTrack;
  openTrack.name = "Open Test";
  {
    editor::Path path;
    path.id = "open-path";
    path.closed = false;
    for (int i = 0; i < 8; ++i) {
      editor::TrackPoint point;
      point.kind = editor::PointKind::Position;
      point.pos = tox::Vec3(static_cast<double>(i) * 100.0, 0.0, 0.0);
      point.weight = 1.0;
      path.points.push_back(point);
    }
    openTrack.paths.push_back(std::move(path));
  }
  editor::EditorState openState(openTrack);  // constructor backfills point ids (p1..p8)
  result.openSplitDisjoint =
      openState.makeDisjoint(0, 3) && openState.track().paths.size() == 2 && openState.disjointSeams().size() == 1 &&
      openState.disjointSeams()[0].kind == "split-open";
  {
    const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(openState.track()));
    result.openSplitBakes = static_cast<bool>(baked) && baked.track->paths.size() == 2;
  }

  // Deleting one of the two split paths leaves the disjoint seam referencing a path that no
  // longer exists -- pruneStaleReferences (called inside deleteCurrentPath) should drop it.
  openState.setCurrentPathIndex(1);
  result.deleteCurrentPathPrunesDanglingSeam =
      openState.deleteCurrentPath() && openState.track().paths.size() == 1 && openState.disjointSeams().empty();

  // --- join: same path closes; two separate open paths merge into a junction ---
  {
    editor::TrackDefinition closeTrack = openTrack;  // reuse the 8-point open path shape
    editor::EditorState closeState(closeTrack);
    result.joinedSamePathCloses = closeState.joinPathEndpoints(0, false, 0, true) && closeState.track().paths[0].closed;
  }
  {
    editor::TrackDefinition twoPathTrack;
    twoPathTrack.name = "Join Test";
    for (int side = 0; side < 2; ++side) {
      editor::Path path;
      path.id = side == 0 ? "path-a" : "path-b";
      path.closed = false;
      for (int i = 0; i < 4; ++i) {
        editor::TrackPoint point;
        point.kind = editor::PointKind::Position;
        point.pos = tox::Vec3(static_cast<double>(side * 1000 + i * 100), 0.0, 0.0);
        point.weight = 1.0;
        path.points.push_back(point);
      }
      twoPathTrack.paths.push_back(std::move(path));
    }
    editor::EditorState joinState(twoPathTrack);
    result.joinedCrossPathCreatesJunction = joinState.joinPathEndpoints(0, true, 1, false) && joinState.junctions().size() == 1;
    const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(joinState.track()));
    result.joinedCrossPathBakes = static_cast<bool>(baked);
  }

  return result;
}

// Gap-6 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #6): direction toggle and
// start-point selection, mirroring #dirBtn and the properties panel's #startBtn. Confirms
// toggleStartReverse/setStartPoint reach undo/redo and, unlike a pure schema-plumbing check, that
// the reversal actually flips which way the baked starting grid faces (StartGrid::startingGridPoses'
// forward direction, not just track_.start.reverse in memory).
struct Gap6SmokeCheckResult {
  bool toggled = false, toggleUndone = false, toggleRedone = false;
  bool startMoved = false, startMoveNoOpWhenAlreadyStart = false, startMoveUndone = false;
  bool bakedGridReversed = false;
};

Gap6SmokeCheckResult runGap6SmokeCheck() {
  Gap6SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  const bool originalReverse = state.track().start.reverse;
  state.toggleStartReverse();
  result.toggled = state.track().start.reverse != originalReverse;
  result.toggleUndone = state.undo() && state.track().start.reverse == originalReverse;
  result.toggleRedone = state.redo() && state.track().start.reverse != originalReverse;

  // "Set as start point": select a different position point on the starter loop and set it start.
  state.selectPoint(0, 4);
  const editor::Start before = state.track().start;
  result.startMoved = state.setStartPoint() && (state.track().start.path != before.path || state.track().start.point != before.point);
  result.startMoveNoOpWhenAlreadyStart = !state.setStartPoint();  // already the start -- no-op
  result.startMoveUndone = state.undo() && state.track().start.path == before.path && state.track().start.point == before.point;

  // Bake the same track with start.reverse toggled and compare the first starting-grid slot's
  // forward direction -- this is what actually changes for the driver, not just a bool in the JSON.
  editor::TrackDefinition forwardDef = state.track();
  forwardDef.start.reverse = false;
  editor::TrackDefinition reverseDef = forwardDef;
  reverseDef.start.reverse = true;
  const tox::TrackLoadResult forwardBaked = tox::Track::fromJson(editor::toJson(forwardDef));
  const tox::TrackLoadResult reverseBaked = tox::Track::fromJson(editor::toJson(reverseDef));
  if (forwardBaked && reverseBaked) {
    tox::Simulation forwardSim(*forwardBaked.track);
    tox::Simulation reverseSim(*reverseBaked.track);
    const std::vector<tox::Pose> forwardPoses = tox::StartGrid::startingGridPoses(forwardSim, *forwardBaked.track, 1);
    const std::vector<tox::Pose> reversePoses = tox::StartGrid::startingGridPoses(reverseSim, *reverseBaked.track, 1);
    if (!forwardPoses.empty() && !reversePoses.empty()) {
      const double dot = forwardPoses[0].forward.x * reversePoses[0].forward.x + forwardPoses[0].forward.y * reversePoses[0].forward.y +
                          forwardPoses[0].forward.z * reversePoses[0].forward.z;
      result.bakedGridReversed = dot < 0.0;
    }
  }

  return result;
}

// Gap-7 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #7): the handling panel
// (maxSpeed/accel/turnSpeed/weight), mirroring #handlingPanel's field-change handler and
// #handlingResetBtn. Confirms setHandling clamps to the same ranges
// TrackCore.normalizeHandling/fromJson use, undo/redo work, and resetHandling restores defaults --
// plus that an edited value actually reaches the physics bake (tox::Track::handling), not just the
// editor's own schema.
struct Gap7SmokeCheckResult {
  bool edited = false, clamped = false, undone = false, redone = false, reset = false;
  bool bakedHandlingMatches = false;
};

Gap7SmokeCheckResult runGap7SmokeCheck() {
  Gap7SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  state.setHandling(200.0, 90.0, 200.0, 1200.0);
  const editor::Handling& h1 = state.track().handling;
  result.edited = h1.maxSpeed == 200.0 && h1.accel == 90.0 && h1.turnSpeed == 200.0 && h1.weight == 1200.0;

  state.setHandling(5000.0, -10.0, 900.0, 1.0);  // out of range on every field
  const editor::Handling& h2 = state.track().handling;
  result.clamped = h2.maxSpeed == 1000.0 && h2.accel == 5.0 && h2.turnSpeed == 720.0 && h2.weight == 50.0;

  result.undone = state.undo() && state.track().handling.maxSpeed == 200.0;
  result.redone = state.redo() && state.track().handling.maxSpeed == 1000.0;

  state.resetHandling();
  const editor::Handling& h3 = state.track().handling;
  result.reset = h3.maxSpeed == 140.0 && h3.accel == 71.0 && h3.turnSpeed == 137.5 && h3.weight == 1000.0;

  state.setHandling(180.0, 60.0, 160.0, 900.0);
  const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(state.track()));
  result.bakedHandlingMatches = static_cast<bool>(baked) && baked.track->definition.handling.maxSpeed == 180.0 &&
                                 baked.track->definition.handling.accel == 60.0 && baked.track->definition.handling.turnSpeed == 160.0 &&
                                 baked.track->definition.handling.weight == 900.0;

  return result;
}

// Gap-9 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #9): top-down grid display, grid
// size, and snap-to-grid, mirroring editor.js's showGrid/gridSize/snapToGrid module state and
// snapWorldXZ(). Exercises TopDownView::snapWorldXZ directly (no ImGui needed -- it's pure view
// state) and EditorState::createModeClick's snapped-point overload, confirming a click near an
// existing draft point still closes/finishes the draft (hit-testing stays unsnapped) while a
// genuinely new point lands on the grid.
struct Gap9SmokeCheckResult {
  bool noSnapByDefault = false, snapOnlyWhenGridShownAndSnapEnabled = false, hiddenGridDisablesSnap = false;
  bool respectsGridSize = false;
  bool createClickSnapsNewPoint = false, createClickClosingStaysUnsnapped = false;
};

Gap9SmokeCheckResult runGap9SmokeCheck() {
  Gap9SmokeCheckResult result;

  editor::TopDownView view;
  const editor::WorldPoint2D raw{37.0, -53.0};
  result.noSnapByDefault = view.snapWorldXZ(raw).x == raw.x && view.snapWorldXZ(raw).z == raw.z;  // snap starts off

  view.setSnapToGrid(true);
  view.setGridSize(32.0);
  const editor::WorldPoint2D snapped32 = view.snapWorldXZ(raw);
  result.snapOnlyWhenGridShownAndSnapEnabled = snapped32.x == 32.0 && snapped32.z == -64.0;  // round(37/32)*32, round(-53/32)*32

  view.setShowGrid(false);
  result.hiddenGridDisablesSnap = view.snapWorldXZ(raw).x == raw.x && view.snapWorldXZ(raw).z == raw.z;
  view.setShowGrid(true);

  view.setGridSize(8.0);
  const editor::WorldPoint2D snapped8 = view.snapWorldXZ(raw);
  result.respectsGridSize = snapped8.x == 40.0 && snapped8.z == -56.0;  // round(37/8)*8, round(-53/8)*8

  editor::EditorState createState(editor::TrackDefinition{});
  createState.createModeClick(100.0, 100.0, 1.0, 96.0, 96.0);  // snapped point differs from raw
  createState.createModeClick(200.0, 100.0, 1.0, 200.0, 100.0);
  createState.createModeClick(200.0, 200.0, 1.0, 200.0, 200.0);
  createState.createModeClick(100.0, 200.0, 1.0, 100.0, 200.0);  // finishCreateDraft needs 4+ points
  result.createClickSnapsNewPoint = createState.createDraft().size() == 4 && createState.createDraft()[0].x == 96.0 &&
                                     createState.createDraft()[0].z == 96.0;
  // Click near the stored (already-snapped) first draft point using the raw hit-test coordinate,
  // but pass a deliberately bogus "snapped" pair -- if the draft closes anyway, that proves
  // hit-testing used the raw click, not the snapped argument only meant for a genuinely new point.
  result.createClickClosingStaysUnsnapped = createState.createModeClick(96.4, 95.7, 1.0, 999.0, 999.0);

  return result;
}

// Gap-14 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #14): undo/redo disabled state,
// mirroring editor.html's #undoBtn/#redoBtn -- disabled while their stack is empty rather than
// always active. Exercises EditorHistory::canUndo/canRedo directly through the same mutation/undo/
// redo calls the UI's BeginDisabled guards now read.
struct Gap14SmokeCheckResult {
  bool emptyAtStart = false, undoEnabledAfterEdit = false, redoDisabledAfterEdit = false;
  bool redoEnabledAfterUndo = false, undoDisabledAfterUndoingEverything = false;
};

Gap14SmokeCheckResult runGap14SmokeCheck() {
  Gap14SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  result.emptyAtStart = !state.history().canUndo() && !state.history().canRedo();

  state.setTrackName("Gap14 Test");
  result.undoEnabledAfterEdit = state.history().canUndo();
  result.redoDisabledAfterEdit = !state.history().canRedo();

  state.undo();
  result.redoEnabledAfterUndo = state.history().canRedo();
  result.undoDisabledAfterUndoingEverything = !state.history().canUndo();

  return result;
}

// Gap-8 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #8): the random-ranges panel.
// `generateRandomTrack` already accepted a `RandomTrackRanges` parameter (M7a/M7c); main.cpp
// simply never passed anything but the `{}` default until this gap's UI wiring. Confirms a custom
// range actually reaches the generator -- pinning turnsMin == turnsMax forces the single-loop
// variant's control-point count to that exact value regardless of complexity (`n` in
// generateRandomTrack, RandomTrack.cpp:283, collapses to `turnsMin` when the range has zero
// width), and disabling mesh sections (meshChanceMax=0, maxMeshSections=0) keeps the whole track a
// single loop so the count is unambiguous -- and that the default-constructed ranges still bake
// cleanly (regression check: this is what every prior random-track call implicitly used). The
// panel's own field-clamping (`sanitize`, a direct port of sanitizeRandomRanges) is UI-only logic
// exercised through ImGui widgets with no headless entry point, consistent with how the other gap
// panels (Zones/Triggers/Properties) aren't unit-tested directly either -- only the
// EditorState/generator side each one drives is.
struct Gap8SmokeCheckResult {
  bool customTurnCountRespected = false, defaultRangesStillBake = false;
};

Gap8SmokeCheckResult runGap8SmokeCheck() {
  Gap8SmokeCheckResult result;

  editor::RandomTrackRanges fixedTurns;
  fixedTurns.turnsMin = fixedTurns.turnsMax = 9;
  fixedTurns.meshChanceMin = fixedTurns.meshChanceMax = 0.0;
  fixedTurns.maxMeshSections = 0;
  const editor::TrackDefinition track = editor::generateRandomTrack(5, 777u, fixedTurns);
  int positionCount = 0;
  if (!track.paths.empty())
    for (const auto& point : track.paths[0].points)
      if (point.kind == editor::PointKind::Position) ++positionCount;
  result.customTurnCountRespected = track.paths.size() == 1 && positionCount == 9;

  const editor::TrackDefinition defaultTrack = editor::generateRandomTrack(5, 777u, editor::RandomTrackRanges{});
  const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(defaultTrack));
  result.defaultRangesStillBake = static_cast<bool>(baked);

  return result;
}

// MppModel smoke check: since MppModelExport.cpp deliberately doesn't link mpp::ModelSerializer
// (see MppModelExport.hpp), there's no real mpp::ModelSerializer::load() available here to
// round-trip through. Instead this parses the emitted bytes with a small structural reader
// written directly against MPPMODEL_EXPORT_SPEC.md's documented layout (independently of
// MppModelExport.cpp's own writer code) and cross-checks every field against the source
// tox::Track -- header magic/version/flags, all six directory entries, and every mesh's
// name/primitive-type/primitive-count/material/vertex-count/stride/index-width/data-sizes.
struct MppModelReadResult {
  bool ok = false;
  std::string error;
  std::uint32_t versionMajor = 0, versionMinor = 0, flags = 0;
  struct Mesh {
    std::string name, material;
    std::uint32_t primitiveType = 0, primitiveCount = 0;
    std::uint32_t vertexCount = 0, vertexStride = 0, vertexDataSize = 0;
    std::uint32_t indexWidth = 0, indexDataSize = 0;
  };
  std::vector<Mesh> meshes;
};

MppModelReadResult readMppModelStructurally(const std::string& bytes) {
  MppModelReadResult result;
  std::size_t pos = 0;
  auto need = [&](std::size_t n) { return pos + n <= bytes.size(); };
  auto u16 = [&]() { std::uint16_t v; std::memcpy(&v, bytes.data() + pos, 2); pos += 2; return v; };
  auto u32 = [&]() { std::uint32_t v; std::memcpy(&v, bytes.data() + pos, 4); pos += 4; return v; };
  auto str = [&]() {
    const std::uint32_t len = u32();
    std::string s = bytes.substr(pos, len);
    pos += len;
    return s;
  };

  if (!need(12) || bytes.compare(0, 4, "MPPM") != 0) { result.error = "bad header/magic"; return result; }
  pos = 4;
  result.versionMajor = u16();
  result.versionMinor = u16();
  result.flags = u32();

  struct Entry { std::uint32_t type, start, end, count; };
  Entry entries[6];
  for (auto& e : entries) {
    if (!need(16)) { result.error = "truncated directory"; return result; }
    e.type = u32();
    e.start = u32();
    e.end = u32();
    e.count = u32();
  }
  const Entry& vertexDir = entries[3];
  const Entry& indexDir = entries[4];
  const Entry& meshDir = entries[5];

  pos = meshDir.start;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> streamIds;  // (vertexStreamId, indexStreamId) per mesh
  for (std::uint32_t i = 0; i < meshDir.count; ++i) {
    MppModelReadResult::Mesh mesh;
    mesh.name = str();
    mesh.primitiveType = u32();
    mesh.primitiveCount = u32();
    mesh.material = str();
    const std::uint32_t numVertexBuffers = u32();
    if (numVertexBuffers != 1) { result.error = "expected exactly one vertex buffer per mesh"; return result; }
    const std::uint32_t vertexStreamId = u32();
    const std::uint32_t indexStreamId = u32();
    streamIds.push_back({vertexStreamId, indexStreamId});
    result.meshes.push_back(mesh);
  }
  if (pos != meshDir.end) { result.error = "mesh metadata section size mismatch"; return result; }

  pos = vertexDir.start;
  for (std::uint32_t i = 0; i < vertexDir.count; ++i) {
    const std::uint32_t dataSize = u32();
    const std::uint32_t vertexCount = u32();
    const std::uint32_t stride = u32();
    pos += dataSize;
    if (i < result.meshes.size() && streamIds[i].first == i) {
      result.meshes[i].vertexCount = vertexCount;
      result.meshes[i].vertexStride = stride;
      result.meshes[i].vertexDataSize = dataSize;
    }
  }
  if (pos != vertexDir.end) { result.error = "vertex data section size mismatch"; return result; }

  pos = indexDir.start;
  for (std::uint32_t i = 0; i < indexDir.count; ++i) {
    const std::uint32_t dataSize = u32();
    const std::uint32_t indexWidth = u32();
    pos += dataSize;
    if (i < result.meshes.size() && streamIds[i].second == i) {
      result.meshes[i].indexWidth = indexWidth;
      result.meshes[i].indexDataSize = dataSize;
    }
  }
  if (pos != indexDir.end) { result.error = "index data section size mismatch"; return result; }

  result.ok = true;
  return result;
}

struct MppModelSmokeCheckResult {
  bool headerOk = false, meshCountMatches = false, fieldsMatch = false, byteSizesMatch = false, wideIndexChosenForLargeMesh = false;
};

MppModelSmokeCheckResult runMppModelSmokeCheck() {
  MppModelSmokeCheckResult result;

  const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(buildStarterTrack()));
  if (!baked) return result;

  const editor::MppModelExportResult exported = editor::exportTrackToMppModel(*baked.track);
  const MppModelReadResult read = readMppModelStructurally(exported.bytes);
  if (!read.ok) return result;

  result.headerOk = read.versionMajor == 1 && read.versionMinor == 1 && read.flags == 0x0001;
  result.meshCountMatches = read.meshes.size() == baked.track->geometry.size() && read.meshes.size() == exported.meshCount;

  bool fieldsMatch = result.meshCountMatches;
  bool byteSizesMatch = true;
  for (std::size_t i = 0; i < read.meshes.size() && fieldsMatch; ++i) {
    const tox::GeometryBatch& batch = baked.track->geometry[i];
    const auto& mesh = read.meshes[i];
    if (mesh.name != batch.id || mesh.material != batch.materialKey) fieldsMatch = false;
    if (mesh.primitiveType != 2 /* Triangles */ || mesh.primitiveCount != batch.indices.size() / 3) fieldsMatch = false;
    if (mesh.vertexCount != batch.vertices.size() || mesh.vertexStride != 36) fieldsMatch = false;
    if (mesh.vertexDataSize != mesh.vertexCount * 36) byteSizesMatch = false;
    const std::uint32_t expectedIndexWidth = batch.vertices.size() > 65535 ? 32 : 16;
    if (mesh.indexWidth != expectedIndexWidth) fieldsMatch = false;
    if (mesh.indexDataSize != batch.indices.size() * (mesh.indexWidth / 8)) byteSizesMatch = false;
  }
  result.fieldsMatch = fieldsMatch;
  result.byteSizesMatch = byteSizesMatch;

  // Confirm the >65535-vertex branch actually selects 32-bit indices, not just the (much more
  // common) 16-bit path every real track batch takes.
  tox::GeometryBatch wideBatch;
  wideBatch.id = "wide-test";
  wideBatch.materialKey = "road";
  wideBatch.vertices.resize(70000);
  wideBatch.indices = {0, 1, 2};
  tox::Track wideTrack;
  wideTrack.geometry.push_back(wideBatch);
  const editor::MppModelExportResult wideExported = editor::exportTrackToMppModel(wideTrack);
  const MppModelReadResult wideRead = readMppModelStructurally(wideExported.bytes);
  result.wideIndexChosenForLargeMesh = wideRead.ok && !wideRead.meshes.empty() && wideRead.meshes[0].indexWidth == 32;

  return result;
}

editor::TrackDefinition buildOpenTestTrack(int pointCount) {
  editor::TrackDefinition track;
  track.name = "Gap11 Open Test";
  editor::Path path;
  path.id = "open-path";
  path.closed = false;
  for (int i = 0; i < pointCount; ++i) {
    editor::TrackPoint point;
    point.kind = editor::PointKind::Position;
    point.pos = tox::Vec3(static_cast<double>(i) * 100.0, 0.0, 0.0);
    point.weight = 1.0;
    path.points.push_back(point);
  }
  track.paths.push_back(std::move(path));
  return track;
}

// Gap-11 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #11): segment selection,
// deletion, splitting, and insert-point-on-segment, mirroring segSel/deleteSegment/
// selectedOutgoingSegment/selectedIncomingSegment/insertNear. Deliberately does not exercise
// segmentAtTop (click-to-select-a-segment) -- js/editor.js defines it but never calls it, so it's
// not ported here either (see EditorState.hpp's comment on the port).
struct Gap11SmokeCheckResult {
  bool outgoingNulloptOnAuxSelection = false, outgoingNulloptAtOpenPathEnd = false, incomingNulloptAtOpenPathStart = false;
  bool closedSegmentDeleteOpensPath = false, closedSegmentDeleteKeepsAllPoints = false;
  bool openFirstSegmentShrinks = false, openFloorGuardHolds = false;
  bool openMiddleSegmentSplits = false, openMiddleSplitGuardHolds = false;
  bool disjointSeamGuardHolds = false;
  bool insertOnSegmentAddsPoint = false, insertedPointBakes = false;
};

Gap11SmokeCheckResult runGap11SmokeCheck() {
  Gap11SmokeCheckResult result;

  // --- selectedOutgoingSegment/selectedIncomingSegment edge cases ---
  {
    editor::EditorState state(buildStarterTrack());  // closed, 12 position points
    const auto rollIndex = state.addAuxPoint(0, editor::PointKind::Roll, 0.5);
    result.outgoingNulloptOnAuxSelection = rollIndex.has_value() && !state.selectedOutgoingSegment().has_value();

    editor::EditorState openState(buildOpenTestTrack(6));
    openState.selectPoint(0, 5);  // last point of an open path
    result.outgoingNulloptAtOpenPathEnd = !openState.selectedOutgoingSegment().has_value();
    openState.selectPoint(0, 0);  // first point of an open path
    result.incomingNulloptAtOpenPathStart = !openState.selectedIncomingSegment().has_value();
  }

  // --- closed path: deleting a segment opens the loop, keeping every point ---
  {
    editor::EditorState state(buildStarterTrack());
    const std::size_t countBefore = state.track().paths[0].points.size();
    state.selectPoint(0, 0);
    const auto seg = state.selectedOutgoingSegment();
    result.closedSegmentDeleteOpensPath =
        seg.has_value() && state.deleteSegmentAt(seg->pathIndex, seg->i) && !state.track().paths[0].closed;
    result.closedSegmentDeleteKeepsAllPoints = state.track().paths[0].points.size() == countBefore;
  }

  // --- open path: first/last segment shrinks; floor refuses below 4 ---
  {
    editor::EditorState state(buildOpenTestTrack(5));
    result.openFirstSegmentShrinks = state.deleteSegmentAt(0, 0) && editor::EditorState::positionCount(state.track().paths[0]) == 4;
    result.openFloorGuardHolds = !state.deleteSegmentAt(0, 0);  // exactly at the 4-point floor now
  }

  // --- open path: an interior segment splits into two paths ---
  {
    editor::EditorState state(buildOpenTestTrack(8));
    result.openMiddleSegmentSplits = state.deleteSegmentAt(0, 3) && state.track().paths.size() == 2 &&
                                      editor::EditorState::positionCount(state.track().paths[0]) == 4 &&
                                      editor::EditorState::positionCount(state.track().paths[1]) == 4;

    editor::EditorState guardState(buildOpenTestTrack(7));  // splitting at i=1 leaves a 2-point half
    result.openMiddleSplitGuardHolds = !guardState.deleteSegmentAt(0, 1);
  }

  // --- disjoint seam guard ---
  {
    editor::EditorState state(buildStarterTrack());
    result.disjointSeamGuardHolds = state.makeDisjoint(0, 3) && !state.deleteSegmentAt(0, 0);
  }

  // --- insert-point-on-segment ---
  {
    editor::EditorState state(buildOpenTestTrack(4));
    const int before = editor::EditorState::positionCount(state.track().paths[0]);
    const auto inserted = state.insertPositionOnSegment(0, 2, 150.0, 3.0, 25.0);
    result.insertOnSegmentAddsPoint = inserted.has_value() && editor::EditorState::positionCount(state.track().paths[0]) == before + 1 &&
                                       state.track().paths[0].points[*inserted].pos.x == 150.0;
    const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(state.track()));
    result.insertedPointBakes = static_cast<bool>(baked);
  }

  return result;
}

// Gap-10 smoke check (EDITOR_PARITY_FIXES.md "Functional gaps" #10): render modes, point-type
// filters, and the physics-sample overlay. Exercises TopDownView's new state directly (pure view
// state, no ImGui needed -- same reasoning as gap 9's snapWorldXZ check); the render-mode fill
// color formulas (rollFillColor/elevationFillColor) and physicsPointAtWorld/
// drawPhysicsSampleInfo are ImGui-adjacent glue with no headless entry point, verified by
// inspection against js/editor.js's rollColor/elevationColor/physicsPointAtTop -- same tradeoff
// already taken for gap 8's sanitize() and gap 13's nearestPathPlacement().
struct Gap10SmokeCheckResult {
  bool defaultRenderModeIsBanked = false, renderModeRoundTrips = false;
  bool pointFiltersDefaultShown = false, positionFilterToggles = false;
  bool physicsPointsHiddenByDefault = false, physicsSelectionRoundTrips = false, hidingPhysicsClearsSelection = false;
};

Gap10SmokeCheckResult runGap10SmokeCheck() {
  Gap10SmokeCheckResult result;

  editor::TopDownView view;
  result.defaultRenderModeIsBanked = view.renderMode() == editor::TopDownView::RenderMode::Banked;
  view.setRenderMode(editor::TopDownView::RenderMode::Elevation);
  result.renderModeRoundTrips = view.renderMode() == editor::TopDownView::RenderMode::Elevation;

  result.pointFiltersDefaultShown =
      view.showPositionPoints() && view.showRollPoints() && view.showWidthPoints() && view.showCrossSectionPoints();
  view.setShowPositionPoints(false);
  result.positionFilterToggles = !view.showPositionPoints();

  result.physicsPointsHiddenByDefault = !view.showPhysicsPoints() && !view.physicsSelection().has_value();
  view.setShowPhysicsPoints(true);
  view.selectPhysicsSample(2, 17);
  result.physicsSelectionRoundTrips =
      view.physicsSelection().has_value() && view.physicsSelection()->pathIndex == 2 && view.physicsSelection()->frameIndex == 17;
  view.setShowPhysicsPoints(false);
  result.hidingPhysicsClearsSelection = !view.physicsSelection().has_value();

  return result;
}

}  // namespace

int main(int, char**) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  const char* glslVersion = "#version 130";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  const SDL_WindowFlags windowFlags =
      static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Window* window =
      SDL_CreateWindow("track_editor (M10: texture file picker)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800,
                       windowFlags);
  if (window == nullptr) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_GLContext glContext = SDL_GL_CreateContext(window);
  if (glContext == nullptr) {
    std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_GL_MakeCurrent(window, glContext);
  SDL_GL_SetSwapInterval(1);  // vsync

  if (gl3wInit() != 0) {
    std::fprintf(stderr, "gl3wInit failed\n");
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // No imgui.ini: the dock layout built once below (menu bar / toolbar / left panel / top-down
  // top-right / elevation bottom-right) is meant to be the SAME fixed arrangement every launch,
  // not something that drifts based on whatever a prior session happened to leave docked where.
  io.IniFilename = nullptr;

  ImGui::StyleColorsDark();

  ImGui_ImplSDL2_InitForOpenGL(window, glContext);
  ImGui_ImplOpenGL3_Init(glslVersion);

  const SmokeCheckResult smoke = runSmokeCheck();
  std::fprintf(stdout, "M1 smoke check: roundTrip=%s bake=%s (%zu paths, %zu geom batches, %zu warnings)%s undoRedo=%s\n",
               smoke.roundTripOk ? "OK" : "MISMATCH", smoke.bakeOk ? "OK" : "FAILED", smoke.pathCount, smoke.geometryBatchCount,
               smoke.warningCount, smoke.bakeOk ? "" : (" error=" + smoke.bakeError).c_str(), smoke.undoRedoOk ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const M3SmokeCheckResult m3Smoke = runM3SmokeCheck();
  std::fprintf(stdout,
               "M3 smoke check: drag=%s dragUndo=%s dragRedo=%s deleteGuard=%s deleteRemoves=%s createDraftCloses=%s\n",
               m3Smoke.dragMovedPoint ? "OK" : "MISMATCH", m3Smoke.dragUndoRestored ? "OK" : "MISMATCH",
               m3Smoke.dragRedoReapplied ? "OK" : "MISMATCH", m3Smoke.deleteGuardHeld ? "OK" : "MISMATCH",
               m3Smoke.deleteRemovedPoint ? "OK" : "MISMATCH", m3Smoke.createDraftMadeClosedPath ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const M4SmokeCheckResult m4Smoke = runM4SmokeCheck();
  std::fprintf(stdout, "M4 smoke check: place=%s drag=%s rotate=%s bakedRegionMoved=%s delete=%s\n",
               m4Smoke.placed ? "OK" : "MISMATCH", m4Smoke.dragMoved ? "OK" : "MISMATCH", m4Smoke.rotateApplied ? "OK" : "MISMATCH",
               m4Smoke.bakedRegionMoved ? "OK" : "MISMATCH", m4Smoke.deleted ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const M5SmokeCheckResult m5Smoke = runM5SmokeCheck();
  std::fprintf(stdout, "M5 smoke check: toggle=%s selectionSet=%s undo=%s redo=%s\n", m5Smoke.toggled ? "OK" : "MISMATCH",
               m5Smoke.selectionSet ? "OK" : "MISMATCH", m5Smoke.undone ? "OK" : "MISMATCH", m5Smoke.redone ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const M6SmokeCheckResult m6Smoke = runM6SmokeCheck();
  std::fprintf(stdout, "M6 smoke check: elevationDrag=%s undo=%s redo=%s\n", m6Smoke.elevationChanged ? "OK" : "MISMATCH",
               m6Smoke.undone ? "OK" : "MISMATCH", m6Smoke.redone ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const M7aSmokeCheckResult m7aSmoke = runM7aSmokeCheck();
  std::fprintf(stdout, "M7a smoke check: randomBake=%s (%zu paths, %zu geom batches) usdHeader=%s usdMeshes=%s (%zu meshes)\n",
               m7aSmoke.randomBakeOk ? "OK" : "FAILED", m7aSmoke.randomPathCount, m7aSmoke.randomGeometryBatchCount,
               m7aSmoke.usdHeaderOk ? "OK" : "MISMATCH", m7aSmoke.usdHasMeshes ? "OK" : "MISMATCH", m7aSmoke.usdMeshCount);
  std::fflush(stdout);

  const MppModelSmokeCheckResult mppModelSmoke = runMppModelSmokeCheck();
  std::fprintf(stdout, "MppModel smoke check: header=%s meshCount=%s fields=%s byteSizes=%s wideIndex=%s\n",
               mppModelSmoke.headerOk ? "OK" : "MISMATCH", mppModelSmoke.meshCountMatches ? "OK" : "MISMATCH",
               mppModelSmoke.fieldsMatch ? "OK" : "MISMATCH", mppModelSmoke.byteSizesMatch ? "OK" : "MISMATCH",
               mppModelSmoke.wideIndexChosenForLargeMesh ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const M7bSmokeCheckResult m7bSmoke = runM7bSmokeCheck();
  std::fprintf(stdout, "M7b smoke check: imageSize=%s add=%s assign=%s tileResize=%s invalidClear=%s delete=%s\n",
               m7bSmoke.imageSizeReadOk ? "OK" : "MISMATCH", m7bSmoke.assetAdded ? "OK" : "MISMATCH", m7bSmoke.assigned ? "OK" : "MISMATCH",
               m7bSmoke.tileResizeOk ? "OK" : "MISMATCH", m7bSmoke.invalidAssignmentCleared ? "OK" : "MISMATCH",
               m7bSmoke.deleted ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const M7cSmokeCheckResult m7cSmoke = runM7cSmokeCheck();
  std::fprintf(stdout, "M7c smoke check: foundMeshSectionSeed=%s bake=%s (%zu paths, %zu mesh assets, %zu placements, %zu warnings)\n",
               m7cSmoke.foundMeshSectionSeed ? "OK" : "FAILED (no seed in [1,200] rolled a mesh section)",
               m7cSmoke.bakeOk ? "OK" : "FAILED", m7cSmoke.pathCount, m7cSmoke.meshAssetCount, m7cSmoke.meshPlacementCount,
               m7cSmoke.warningCount);
  std::fflush(stdout);

  const M9SmokeCheckResult m9Smoke = runM9SmokeCheck();
  std::fprintf(stdout, "M9 smoke check: parse=%s import=%s railedBoundary=%s bakesCleanly=%s badJsonRejected=%s\n",
               m9Smoke.parsedFromJson ? "OK" : "MISMATCH", m9Smoke.imported ? "OK" : "MISMATCH",
               m9Smoke.railedBoundary ? "OK" : "MISMATCH", m9Smoke.bakesCleanly ? "OK" : "MISMATCH",
               m9Smoke.badJsonRejected ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const ParitySmokeCheckResult paritySmoke = runParitySmokeCheck();
  std::fprintf(stdout,
               "Parity-fix smoke check: noIdCollision=%s drawnPathBakesAsDrawn=%s startPreserved=%s startClamped=%s "
               "orphanAssetPruned=%s\n",
               paritySmoke.noIdCollisionOnCreate ? "OK" : "MISMATCH", paritySmoke.drawnPathBakesAsDrawn ? "OK" : "MISMATCH",
               paritySmoke.startPointPreservedOnDelete ? "OK" : "MISMATCH", paritySmoke.startClampedInRange ? "OK" : "MISMATCH",
               paritySmoke.orphanedMeshAssetPruned ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap1SmokeCheckResult gap1Smoke = runGap1SmokeCheck();
  std::fprintf(stdout,
               "Gap1 smoke check (roll/width/crossSection editing): add=%s/%s/%s edit=%s delete=%s bakedRoll=%s bakedWidth=%s "
               "positionFloorHeld=%s auxUnguarded=%s\n",
               gap1Smoke.rollAdded ? "OK" : "MISMATCH", gap1Smoke.widthAdded ? "OK" : "MISMATCH",
               gap1Smoke.crossSectionAdded ? "OK" : "MISMATCH", gap1Smoke.fieldsEdited ? "OK" : "MISMATCH",
               gap1Smoke.deleted ? "OK" : "MISMATCH", gap1Smoke.bakedRollApplied ? "OK" : "MISMATCH",
               gap1Smoke.bakedWidthApplied ? "OK" : "MISMATCH", gap1Smoke.deletingBelowFourPositionsRefused ? "OK" : "MISMATCH",
               gap1Smoke.deletingAuxPointsUnguarded ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap2SmokeCheckResult gap2Smoke = runGap2SmokeCheck();
  std::fprintf(stdout, "Gap2 smoke check (track name editing): rename=%s undo=%s redo=%s noOpRefused=%s emptyLive=%s emptyFallback=%s\n",
               gap2Smoke.renamed ? "OK" : "MISMATCH", gap2Smoke.undone ? "OK" : "MISMATCH", gap2Smoke.redone ? "OK" : "MISMATCH",
               gap2Smoke.noOpRefused ? "OK" : "MISMATCH", gap2Smoke.emptyNameLiveInMemory ? "OK" : "MISMATCH",
               gap2Smoke.emptyNameFallsBackOnSerialize ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap3SmokeCheckResult gap3Smoke = runGap3SmokeCheck();
  std::fprintf(stdout,
               "Gap3 smoke check (zones): add=%s select=%s bakedPathZone=%s bakedFactor=%s edit=%s startGridAdd=%s "
               "bakesWithBoth=%s delete=%s\n",
               gap3Smoke.added ? "OK" : "MISMATCH", gap3Smoke.selected ? "OK" : "MISMATCH", gap3Smoke.bakedAsPathZone ? "OK" : "MISMATCH",
               gap3Smoke.bakedFactorApplied ? "OK" : "MISMATCH", gap3Smoke.edited ? "OK" : "MISMATCH",
               gap3Smoke.startGridAdded ? "OK" : "MISMATCH", gap3Smoke.bakesWithMultipleZones ? "OK" : "MISMATCH",
               gap3Smoke.deleted ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap4SmokeCheckResult gap4Smoke = runGap4SmokeCheck();
  std::fprintf(stdout,
               "Gap4 smoke check (triggers): add=%s select=%s bakedAsGate=%s edit=%s secondCheckpoint=%s finishUnique=%s "
               "deleteBlocked=%s deleteAfterDemotion=%s\n",
               gap4Smoke.added ? "OK" : "MISMATCH", gap4Smoke.selected ? "OK" : "MISMATCH", gap4Smoke.bakedAsGate ? "OK" : "MISMATCH",
               gap4Smoke.edited ? "OK" : "MISMATCH", gap4Smoke.secondCheckpointAdded ? "OK" : "MISMATCH",
               gap4Smoke.finishUniqueAfterPromotion ? "OK" : "MISMATCH", gap4Smoke.deleteBlockedWhileFinish ? "OK" : "MISMATCH",
               gap4Smoke.deletedAfterDemotion ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap5SmokeCheckResult gap5Smoke = runGap5SmokeCheck();
  std::fprintf(stdout,
               "Gap5 smoke check (curve management): defaultPath=%s clamp=%s closedDisjoint=%s closedBakes=%s closedReconnect=%s "
               "openSplit=%s openSplitBakes=%s deletePrunesSeam=%s joinSameCloses=%s joinCrossJunction=%s joinCrossBakes=%s\n",
               gap5Smoke.defaultCurrentPathIsZero ? "OK" : "MISMATCH", gap5Smoke.clampsWithOnePath ? "OK" : "MISMATCH",
               gap5Smoke.closedMadeDisjoint ? "OK" : "MISMATCH", gap5Smoke.closedBakesOpen ? "OK" : "MISMATCH",
               gap5Smoke.closedReconnected ? "OK" : "MISMATCH", gap5Smoke.openSplitDisjoint ? "OK" : "MISMATCH",
               gap5Smoke.openSplitBakes ? "OK" : "MISMATCH", gap5Smoke.deleteCurrentPathPrunesDanglingSeam ? "OK" : "MISMATCH",
               gap5Smoke.joinedSamePathCloses ? "OK" : "MISMATCH", gap5Smoke.joinedCrossPathCreatesJunction ? "OK" : "MISMATCH",
               gap5Smoke.joinedCrossPathBakes ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap6SmokeCheckResult gap6Smoke = runGap6SmokeCheck();
  std::fprintf(stdout,
               "Gap6 smoke check (direction toggle / start point): toggle=%s undo=%s redo=%s startMoved=%s "
               "noOpAtStart=%s startUndone=%s bakedGridReversed=%s\n",
               gap6Smoke.toggled ? "OK" : "MISMATCH", gap6Smoke.toggleUndone ? "OK" : "MISMATCH",
               gap6Smoke.toggleRedone ? "OK" : "MISMATCH", gap6Smoke.startMoved ? "OK" : "MISMATCH",
               gap6Smoke.startMoveNoOpWhenAlreadyStart ? "OK" : "MISMATCH", gap6Smoke.startMoveUndone ? "OK" : "MISMATCH",
               gap6Smoke.bakedGridReversed ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap7SmokeCheckResult gap7Smoke = runGap7SmokeCheck();
  std::fprintf(stdout, "Gap7 smoke check (handling panel): edit=%s clamp=%s undo=%s redo=%s reset=%s bakedMatches=%s\n",
               gap7Smoke.edited ? "OK" : "MISMATCH", gap7Smoke.clamped ? "OK" : "MISMATCH", gap7Smoke.undone ? "OK" : "MISMATCH",
               gap7Smoke.redone ? "OK" : "MISMATCH", gap7Smoke.reset ? "OK" : "MISMATCH", gap7Smoke.bakedHandlingMatches ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap8SmokeCheckResult gap8Smoke = runGap8SmokeCheck();
  std::fprintf(stdout, "Gap8 smoke check (random ranges panel): customTurnCountRespected=%s defaultRangesStillBake=%s\n",
               gap8Smoke.customTurnCountRespected ? "OK" : "MISMATCH", gap8Smoke.defaultRangesStillBake ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap10SmokeCheckResult gap10Smoke = runGap10SmokeCheck();
  std::fprintf(stdout,
               "Gap10 smoke check (render modes / point filters / physics overlay): defaultBanked=%s "
               "renderModeRoundTrips=%s filtersDefaultShown=%s positionFilterToggles=%s physicsHiddenByDefault=%s "
               "physicsSelectionRoundTrips=%s hidingClearsSelection=%s\n",
               gap10Smoke.defaultRenderModeIsBanked ? "OK" : "MISMATCH", gap10Smoke.renderModeRoundTrips ? "OK" : "MISMATCH",
               gap10Smoke.pointFiltersDefaultShown ? "OK" : "MISMATCH", gap10Smoke.positionFilterToggles ? "OK" : "MISMATCH",
               gap10Smoke.physicsPointsHiddenByDefault ? "OK" : "MISMATCH", gap10Smoke.physicsSelectionRoundTrips ? "OK" : "MISMATCH",
               gap10Smoke.hidingPhysicsClearsSelection ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap11SmokeCheckResult gap11Smoke = runGap11SmokeCheck();
  std::fprintf(stdout,
               "Gap11 smoke check (segment select/delete/split, insert-on-segment): outgoingNulloptOnAux=%s "
               "outgoingNulloptAtEnd=%s incomingNulloptAtStart=%s closedOpensPath=%s closedKeepsPoints=%s "
               "openShrinks=%s openFloorGuard=%s openSplits=%s openSplitGuard=%s disjointGuard=%s insertAdds=%s insertBakes=%s\n",
               gap11Smoke.outgoingNulloptOnAuxSelection ? "OK" : "MISMATCH", gap11Smoke.outgoingNulloptAtOpenPathEnd ? "OK" : "MISMATCH",
               gap11Smoke.incomingNulloptAtOpenPathStart ? "OK" : "MISMATCH", gap11Smoke.closedSegmentDeleteOpensPath ? "OK" : "MISMATCH",
               gap11Smoke.closedSegmentDeleteKeepsAllPoints ? "OK" : "MISMATCH", gap11Smoke.openFirstSegmentShrinks ? "OK" : "MISMATCH",
               gap11Smoke.openFloorGuardHolds ? "OK" : "MISMATCH", gap11Smoke.openMiddleSegmentSplits ? "OK" : "MISMATCH",
               gap11Smoke.openMiddleSplitGuardHolds ? "OK" : "MISMATCH", gap11Smoke.disjointSeamGuardHolds ? "OK" : "MISMATCH",
               gap11Smoke.insertOnSegmentAddsPoint ? "OK" : "MISMATCH", gap11Smoke.insertedPointBakes ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap9SmokeCheckResult gap9Smoke = runGap9SmokeCheck();
  std::fprintf(stdout,
               "Gap9 smoke check (grid display / size / snap): noSnapByDefault=%s snapWhenEnabled=%s "
               "hiddenGridDisablesSnap=%s respectsGridSize=%s createClickSnaps=%s createClickClosingUnsnapped=%s\n",
               gap9Smoke.noSnapByDefault ? "OK" : "MISMATCH", gap9Smoke.snapOnlyWhenGridShownAndSnapEnabled ? "OK" : "MISMATCH",
               gap9Smoke.hiddenGridDisablesSnap ? "OK" : "MISMATCH", gap9Smoke.respectsGridSize ? "OK" : "MISMATCH",
               gap9Smoke.createClickSnapsNewPoint ? "OK" : "MISMATCH", gap9Smoke.createClickClosingStaysUnsnapped ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap14SmokeCheckResult gap14Smoke = runGap14SmokeCheck();
  std::fprintf(stdout,
               "Gap14 smoke check (undo/redo disabled state): emptyAtStart=%s undoEnabledAfterEdit=%s "
               "redoDisabledAfterEdit=%s redoEnabledAfterUndo=%s undoDisabledAfterUndoingEverything=%s\n",
               gap14Smoke.emptyAtStart ? "OK" : "MISMATCH", gap14Smoke.undoEnabledAfterEdit ? "OK" : "MISMATCH",
               gap14Smoke.redoDisabledAfterEdit ? "OK" : "MISMATCH", gap14Smoke.redoEnabledAfterUndo ? "OK" : "MISMATCH",
               gap14Smoke.undoDisabledAfterUndoingEverything ? "OK" : "MISMATCH");
  std::fflush(stdout);

  // The canvas needs a persistent EditorState (authored track + mode/selection/drag/undo-redo)
  // plus its baked preview and view/camera state, all surviving across frames. There is no
  // "new track"/load UI yet (M4+), so the starter track is the only thing on screen.
  editor::EditorState editorState(buildStarterTrack());
  tox::TrackLoadResult bakedResult = tox::Track::fromJson(editor::toJson(editorState.track()));
  const tox::Track* bakedTrack = bakedResult ? &*bakedResult.track : nullptr;
  editor::TopDownView topDownView;
  editor::TextureCache textureCache;
  bool elevationVisible = true;
  int randomSeed = 12345;
  int randomComplexity = 5;
  // Random-track generator ranges (EDITOR_PARITY_FIXES.md gap 8), mirrors editor.js's
  // randomRanges -- a session-only generator preference (see RandomRangesPanel.hpp), not track
  // data, so it lives here rather than in EditorState/undo history.
  editor::RandomTrackRanges randomRanges;
  std::string usdExportStatus;
  std::string mppModelExportStatus;
  std::string fileIoStatus;

  auto rebake = [&]() {
    bakedResult = tox::Track::fromJson(editor::toJson(editorState.track()));
    bakedTrack = bakedResult ? &*bakedResult.track : nullptr;
  };

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) running = false;
      if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
          event.window.windowID == SDL_GetWindowID(window)) {
        running = false;
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // E/C/R switch mode (mirrors editor.js's shortcuts), Ctrl+Z/Ctrl+Y undo/redo -- all global
    // since there's no text-input widget yet that would need to steal these keys.
    if (!io.WantTextInput) {
      if (ImGui::IsKeyPressed(ImGuiKey_E)) editorState.setMode(editor::EditMode::Edit);
      if (ImGui::IsKeyPressed(ImGuiKey_C)) editorState.setMode(editor::EditMode::Create);
      if (ImGui::IsKeyPressed(ImGuiKey_R)) editorState.setMode(editor::EditMode::Rails);
      if (ImGui::IsKeyPressed(ImGuiKey_G)) topDownView.setShowGrid(!topDownView.showGrid());
      const bool ctrl = io.KeyCtrl;
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z) && editorState.undo()) rebake();
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y) && editorState.redo()) rebake();
    }

    // --- Fixed layout: menu bar, toolbar, dockspace (left panel / top-down top-right / elevation
    // bottom-right, EDITOR_CPP_PORT_PLAN.md-adjacent UI pass) -------------------------------
    //
    // File/Random/View menu actions below reuse exactly the same EditorState/TopDownView calls
    // the old single "track_editor — status" mega-window made inline; only their container moved
    // (menu item / toolbar button / Diagnostics panel), not their logic.
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New")) {
          editorState.history().push(editorState.track());
          editorState.replaceTrack(buildStarterTrack());
          rebake();
          fileIoStatus.clear();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import JSON...")) {
          const editor::FileDialogResult picked = editor::showOpenFileDialog(L"Import Track JSON", {{L"Track JSON (*.json)", L"*.json"}});
          if (picked.ok) {
            try {
              editor::TrackDefinition imported = editor::fromFile(picked.path);
              // Push undo of the prior track only on successful parse, mirroring importFile's
              // pushUndo() placement in js/editor.js -- a failed import must never disturb history.
              editorState.history().push(editorState.track());
              editorState.replaceTrack(std::move(imported));
              rebake();
              fileIoStatus = "Loaded " + editor::pathToUtf8(picked.path);
            } catch (const std::exception& error) {
              fileIoStatus = std::string("Import failed: ") + error.what();
            }
          }
        }
        if (ImGui::MenuItem("Export JSON...")) {
          const editor::FileDialogResult picked = editor::showSaveFileDialog(
              L"Export Track JSON", {{L"Track JSON (*.json)", L"*.json"}}, toWide(sanitizeFilenameStem(editorState.track().name) + ".json"), L"json");
          if (picked.ok) {
            // toFile() throws when the stream won't open (read-only target, locked file, removed
            // drive) -- uncaught here it took the whole process down and the user's unsaved track
            // with it (EDITOR_PARITY_FIXES.md finding 3). Import JSON already caught; this didn't.
            try {
              editor::toFile(editorState.track(), picked.path);
              fileIoStatus = "Wrote " + editor::pathToUtf8(picked.path);
            } catch (const std::exception& error) {
              fileIoStatus = std::string("Export failed: ") + error.what();
            }
          }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Place Test Mesh")) {
          // Placed just outside the starter circle (radius ~1333m) so it's never accidentally on
          // top of the track -- there's no asset library/drag-from-palette UI yet (M4 is
          // placement, not authoring), so this is the only way to get a mesh region onto the
          // canvas at all.
          if (editorState.placeMeshAsset("test-rect", 1600.0, 0.0)) rebake();
        }
        // Import Mesh/Paste Mesh mirror editor.html's #importMeshBtn/#pasteMeshBtn
        // (EDITOR_NATIVE_FILE_IO_PLAN.md M9); the right-click "Paste Mesh" in TopDownCanvas.cpp
        // shares EditorState::importMeshFromJsonText with the menu item below.
        if (ImGui::MenuItem("Import Mesh...")) {
          const editor::FileDialogResult picked = editor::showOpenFileDialog(L"Import Mesh JSON", {{L"Mesh JSON (*.json)", L"*.json"}});
          if (picked.ok) {
            std::ifstream input(picked.path, std::ios::binary);
            if (!input) {
              // Previously fell through to importMeshFromJsonText with empty text, which reported
              // the clipboard-flavoured "nothing to import (the clipboard is empty)" for a file
              // that simply couldn't be opened (EDITOR_PARITY_FIXES.md finding 8).
              fileIoStatus = "Mesh import failed: could not open " + editor::pathToUtf8(picked.path);
            } else {
              std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
              const editor::WorldPoint2D center = topDownView.center();
              const auto error = editorState.importMeshFromJsonText(text, editor::pathToUtf8(picked.path.filename()), center.x, center.z);
              if (error)
                fileIoStatus = "Mesh import failed: " + *error;
              else
                rebake();
            }
          }
        }
        if (ImGui::MenuItem("Paste Mesh")) {
          // Unlike Import Mesh (centred on the current view) and the right-click paste (centred
          // on the click), the menu paste has no position to centre on -- mirrors
          // importMeshFromClipboard's own `at = {x:0,z:0}` default when called without centreOn.
          if (const auto text = editor::readClipboardText()) {
            const auto error = editorState.importMeshFromJsonText(*text, "pasted-mesh", 0.0, 0.0);
            if (error)
              fileIoStatus = "Clipboard does not contain a mesh: " + *error;
            else
              rebake();
          } else {
            fileIoStatus = "Could not read the clipboard";
          }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Export USD...")) {
          if (bakedTrack != nullptr) {
            const editor::FileDialogResult picked = editor::showSaveFileDialog(
                L"Export USD", {{L"USD ASCII (*.usda)", L"*.usda"}}, toWide(sanitizeFilenameStem(editorState.track().name) + ".usda"), L"usda");
            if (picked.ok) {
              const editor::USDExportResult usd = editor::exportTrackToUSDA(*bakedTrack);
              std::ofstream out(picked.path, std::ios::binary);
              if (out) {
                out << usd.text;
                usdExportStatus = "Wrote " + editor::pathToUtf8(picked.path) + " (" + std::to_string(usd.meshCount) + " mesh(es))";
              } else {
                usdExportStatus = "Failed to open " + editor::pathToUtf8(picked.path) + " for writing";
              }
            }
          } else {
            usdExportStatus = "Nothing to export -- current track failed to bake";
          }
        }
        // Export .mppmodel (MPPMODEL_EXPORT_SPEC.md), a from-scratch native writer of
        // MassivePolyPusher's binary model format -- see MppModelExport.hpp for why this doesn't
        // link mpp::ModelSerializer itself.
        if (ImGui::MenuItem("Export MppModel...")) {
          if (bakedTrack != nullptr) {
            const editor::FileDialogResult picked =
                editor::showSaveFileDialog(L"Export MppModel", {{L"MassivePolyPusher Model (*.mppmodel)", L"*.mppmodel"}},
                                           toWide(sanitizeFilenameStem(editorState.track().name) + ".mppmodel"), L"mppmodel");
            if (picked.ok) {
              const editor::MppModelExportResult mppModel = editor::exportTrackToMppModel(*bakedTrack);
              std::ofstream out(picked.path, std::ios::binary);
              if (out) {
                out.write(mppModel.bytes.data(), static_cast<std::streamsize>(mppModel.bytes.size()));
                mppModelExportStatus = "Wrote " + editor::pathToUtf8(picked.path) + " (" + std::to_string(mppModel.meshCount) + " mesh(es))";
              } else {
                mppModelExportStatus = "Failed to open " + editor::pathToUtf8(picked.path) + " for writing";
              }
            }
          } else {
            mppModelExportStatus = "Nothing to export -- current track failed to bake";
          }
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Edit")) {
        // Undo/redo disabled state (EDITOR_PARITY_FIXES.md gap 14), mirrors editor.html's
        // #undoBtn/#redoBtn: disabled while their respective stack is empty rather than always
        // active.
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, editorState.history().canUndo())) {
          if (editorState.undo()) rebake();
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, editorState.history().canRedo())) {
          if (editorState.redo()) rebake();
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("View")) {
        // Top-down grid display / grid size / snap-to-grid (EDITOR_PARITY_FIXES.md gap 9),
        // mirrors editor.html's #showGridChk/#gridSizeSelect/#snapGridChk. Hiding the grid
        // disables (but doesn't clear) the size and snap controls -- snapWorldXZ() itself
        // re-checks showGrid, so there's no way to leave snapping silently active behind a
        // hidden grid.
        bool showGrid = topDownView.showGrid();
        if (ImGui::MenuItem("Show Grid", "G", &showGrid)) topDownView.setShowGrid(showGrid);
        if (ImGui::BeginMenu("Grid Size", showGrid)) {
          const int gridSizeOptions[] = {8, 16, 32, 64};
          for (int option : gridSizeOptions) {
            const bool selected = static_cast<double>(option) == topDownView.gridSize();
            if (ImGui::MenuItem(std::to_string(option).c_str(), nullptr, selected)) topDownView.setGridSize(static_cast<double>(option));
          }
          ImGui::EndMenu();
        }
        bool snapToGrid = topDownView.snapToGrid();
        if (ImGui::MenuItem("Snap to Grid", nullptr, &snapToGrid, showGrid)) topDownView.setSnapToGrid(snapToGrid);
        ImGui::Separator();
        // Render mode (EDITOR_PARITY_FIXES.md gap 10), mirrors editor.html's #renderModeSelect.
        if (ImGui::BeginMenu("Render Mode")) {
          const std::pair<const char*, editor::TopDownView::RenderMode> renderModes[] = {
              {"Banked edges (lean tint)", editor::TopDownView::RenderMode::Banked},
              {"Flat width (roll colour)", editor::TopDownView::RenderMode::Flat},
              {"Flat with elevation colour", editor::TopDownView::RenderMode::Elevation},
          };
          for (const auto& [label, mode] : renderModes) {
            if (ImGui::MenuItem(label, nullptr, topDownView.renderMode() == mode)) topDownView.setRenderMode(mode);
          }
          ImGui::EndMenu();
        }
        ImGui::Separator();
        // Point-type filters (EDITOR_PARITY_FIXES.md gap 10), mirrors editor.html's
        // #pointFilters. Only Position currently has an observable effect -- roll/width/
        // crossSection points have no on-canvas presence yet at all (gap 1), so those three
        // checkboxes exist for UI parity but are otherwise inert until that on-canvas rendering
        // lands.
        if (ImGui::BeginMenu("Point Filters")) {
          bool showPosition = topDownView.showPositionPoints();
          if (ImGui::MenuItem("Position", nullptr, &showPosition)) topDownView.setShowPositionPoints(showPosition);
          bool showRoll = topDownView.showRollPoints();
          if (ImGui::MenuItem("Roll", nullptr, &showRoll)) topDownView.setShowRollPoints(showRoll);
          bool showWidth = topDownView.showWidthPoints();
          if (ImGui::MenuItem("Width", nullptr, &showWidth)) topDownView.setShowWidthPoints(showWidth);
          bool showCrossSection = topDownView.showCrossSectionPoints();
          if (ImGui::MenuItem("Cross-Section", nullptr, &showCrossSection)) topDownView.setShowCrossSectionPoints(showCrossSection);
          ImGui::EndMenu();
        }
        ImGui::Separator();
        // Physics-sample overlay (EDITOR_PARITY_FIXES.md gap 10), mirrors editor.html's
        // #showPhysicsBtn/#hidePhysicsBtn.
        bool showPhysicsPoints = topDownView.showPhysicsPoints();
        if (ImGui::MenuItem("Show Physics Points", nullptr, &showPhysicsPoints)) topDownView.setShowPhysicsPoints(showPhysicsPoints);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Random")) {
        ImGui::TextUnformatted("Single-loop generator; see RandomTrack.hpp for scope.");
        ImGui::SetNextItemWidth(160);
        ImGui::InputInt("Seed", &randomSeed);
        ImGui::SetNextItemWidth(160);
        ImGui::SliderInt("Complexity", &randomComplexity, 1, 10);
        if (ImGui::MenuItem("Generate New Random Track")) {
          // Mirrors applyRandomTrack()'s pushUndo(): replaceTrack() alone doesn't touch history,
          // so the pre-generation state has to be pushed explicitly to stay undoable.
          editorState.history().push(editorState.track());
          editorState.replaceTrack(editor::generateRandomTrack(randomComplexity, static_cast<std::uint32_t>(randomSeed), randomRanges));
          rebake();
        }
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    // Toolbar: a fixed strip pinned directly under the menu bar (not part of the dockspace, not
    // movable/resizable) for the handful of controls used constantly regardless of which panel
    // tab is focused -- track identity, direction, mode, and quick undo/redo. Everything else
    // that used to live in the old single "track_editor — status" mega-window moved into the
    // menu bar above or the Diagnostics panel below.
    const float menuBarHeight = ImGui::GetFrameHeight();
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x, mainViewport->WorkPos.y + menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::Begin("##Toolbar", nullptr,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoSavedSettings);
    // Track name (EDITOR_PARITY_FIXES.md gap 2), mirrors editor.html's #nameInput. The buffer
    // only resyncs from editorState.track().name when that value has actually changed since last
    // frame (undo/redo/New/Import all go through setTrackName or replaceTrack, not live typing)
    // -- otherwise a resync every frame would stomp in-progress keystrokes before they're
    // committed.
    {
      static char nameBuf[256] = "";
      static std::string lastSyncedName;
      if (lastSyncedName != editorState.track().name) {
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", editorState.track().name.c_str());
        lastSyncedName = editorState.track().name;
      }
      ImGui::SetNextItemWidth(220);
      ImGui::InputText("Track Name", nameBuf, sizeof(nameBuf));
      if (ImGui::IsItemDeactivatedAfterEdit() && editorState.setTrackName(nameBuf)) lastSyncedName = editorState.track().name;
    }
    // Direction toggle (EDITOR_PARITY_FIXES.md gap 6), mirrors editor.html's #dirBtn.
    ImGui::SameLine();
    if (ImGui::Button(editorState.track().start.reverse ? "Direction: Reversed" : "Direction: Forward")) {
      editorState.toggleStartReverse();
      rebake();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Mode (E/C/R):");
    ImGui::SameLine();
    int modeIndex = static_cast<int>(editorState.mode());
    const char* modeNames[] = {"Edit", "Create", "Rails"};
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("##mode", &modeIndex, modeNames, 3)) editorState.setMode(static_cast<editor::EditMode>(modeIndex));
    ImGui::SameLine();
    ImGui::BeginDisabled(!editorState.history().canUndo());
    if (ImGui::Button("Undo")) {
      if (editorState.undo()) rebake();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!editorState.history().canRedo());
    if (ImGui::Button("Redo")) {
      if (editorState.redo()) rebake();
    }
    ImGui::EndDisabled();
    // Mode-specific hint, and any pending file/export status -- consolidated into one status
    // line rather than scattered SameLine()s after whichever button just ran, since those
    // buttons now live in the File menu, not here.
    switch (editorState.mode()) {
      case editor::EditMode::Edit:
        ImGui::TextUnformatted("Edit mode: click to select a point or mesh region. Drag to move; shift+drag a mesh to rotate. Delete/Backspace removes the selection.");
        break;
      case editor::EditMode::Create:
        ImGui::TextUnformatted("Create mode: click to add points; click the first point to close, the last to finish open. Right-click cancels the draft.");
        break;
      case editor::EditMode::Rails:
        ImGui::TextUnformatted("Rails mode: click near a mesh edge to toggle it as a rail (orange = flagged). A miss pans, same as right-drag elsewhere.");
        break;
    }
    if (!fileIoStatus.empty()) ImGui::TextUnformatted(fileIoStatus.c_str());
    if (!usdExportStatus.empty()) ImGui::TextUnformatted(usdExportStatus.c_str());
    if (!mppModelExportStatus.empty()) ImGui::TextUnformatted(mppModelExportStatus.c_str());
    const float toolbarHeight = ImGui::GetWindowSize().y;
    ImGui::End();
    ImGui::PopStyleVar();

    // Dockspace host: fills the remaining viewport below the toolbar. The layout itself (left
    // panel with every property/tool panel tabbed together, top-down view top-right, elevation
    // profile bottom-right) is built once via DockBuilder on the first frame only, then never
    // touched again -- io.IniFilename is null (see CreateContext above), so there's no saved
    // layout to conflict with, and every future launch starts from this exact same arrangement.
    ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x, mainViewport->WorkPos.y + menuBarHeight + toolbarHeight));
    ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, mainViewport->WorkSize.y - menuBarHeight - toolbarHeight));
    ImGui::SetNextWindowViewport(mainViewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##DockSpaceHost", nullptr,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    const ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    static bool dockLayoutBuilt = false;
    if (!dockLayoutBuilt) {
      dockLayoutBuilt = true;
      ImGui::DockBuilderRemoveNode(dockspaceId);
      ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(dockspaceId, mainViewport->WorkSize);

      ImGuiID leftId = 0, rightId = 0;
      ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.24f, &leftId, &rightId);
      ImGuiID topRightId = 0, bottomRightId = 0;
      ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Up, 0.7f, &topRightId, &bottomRightId);

      ImGui::DockBuilderDockWindow("Panels", leftId);
      ImGui::DockBuilderDockWindow("Top-Down View", topRightId);
      ImGui::DockBuilderDockWindow("Elevation Profile", bottomRightId);

      ImGui::DockBuilderFinish(dockspaceId);
    }
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();

    // Mirrors currentCurve(): see EditorState::currentPathIndex()'s own comment for how "current"
    // is resolved (selection wins while a point is selected; otherwise the curve-selector dropdown).
    // Computed up here (rather than between Top-Down View and this window, as before) since every
    // section below that needs it now lives in the same "Panels" window.
    const int currentPathIndex = editorState.track().paths.empty() ? -1 : editorState.currentPathIndex();

    // Left-docked panel (EDITOR_PARITY_FIXES.md-adjacent UI pass): every property/tool section as
    // a collapsing header in one window, rather than separate tabbed windows -- CollapsingHeader
    // does NOT push an ID scope onto what follows it (unlike TreeNode), so each section's content
    // is wrapped in its own PushID/PopID to keep same-labelled widgets in different sections
    // (e.g. both HandlingPanel and RandomRangesPanel have a "Reset to Default" button) from
    // colliding on ImGui ID.
    ImGui::Begin("Panels");
    if (ImGui::CollapsingHeader("Point Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::PushID("PointProperties");
      if (editor::DrawPropertiesPanel(editorState, currentPathIndex, topDownView, bakedTrack)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Zones")) {
      ImGui::PushID("Zones");
      if (editor::DrawZonesPanel(editorState, currentPathIndex)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Triggers")) {
      ImGui::PushID("Triggers");
      if (editor::DrawTriggersPanel(editorState, currentPathIndex)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Curves")) {
      ImGui::PushID("Curves");
      if (editor::DrawCurvesPanel(editorState)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Textures")) {
      ImGui::PushID("Textures");
      if (editor::DrawTexturePanel(editorState, textureCache, currentPathIndex)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Handling")) {
      ImGui::PushID("Handling");
      if (editor::DrawHandlingPanel(editorState)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Random Ranges")) {
      ImGui::PushID("RandomRanges");
      editor::DrawRandomRangesPanel(randomRanges);
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Diagnostics")) {
    ImGui::PushID("Diagnostics");
    ImGui::TextUnformatted("SDL2 + OpenGL3 + ImGui (docking) + gl3w link up.");
    ImGui::Separator();
    ImGui::TextUnformatted("Startup smoke check (starter track -> EditorTrackDefinition -> JSON):");
    ImGui::BulletText("JSON round-trip (toJson . fromJson . toJson idempotent): %s", smoke.roundTripOk ? "OK" : "MISMATCH");
    if (smoke.bakeOk) {
      ImGui::BulletText("tox::Track::fromJson bake: OK (%zu path(s), %zu geometry batch(es), %zu warning(s))", smoke.pathCount,
                        smoke.geometryBatchCount, smoke.warningCount);
    } else {
      ImGui::BulletText("tox::Track::fromJson bake: FAILED (%s)", smoke.bakeError.c_str());
    }
    ImGui::BulletText("EditorHistory undo/redo round trip: %s", smoke.undoRedoOk ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("M3 smoke check (EditorState logic, exercised directly):");
    ImGui::BulletText("drag moves point / undo restores / redo reapplies: %s / %s / %s", m3Smoke.dragMovedPoint ? "OK" : "MISMATCH",
                      m3Smoke.dragUndoRestored ? "OK" : "MISMATCH", m3Smoke.dragRedoReapplied ? "OK" : "MISMATCH");
    ImGui::BulletText("delete removes point / 4-point floor guard holds: %s / %s", m3Smoke.deleteRemovedPoint ? "OK" : "MISMATCH",
                      m3Smoke.deleteGuardHeld ? "OK" : "MISMATCH");
    ImGui::BulletText("create-mode draft closes into a new path: %s", m3Smoke.createDraftMadeClosedPath ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("M4 smoke check (mesh placement, exercised directly):");
    ImGui::BulletText("place / drag / rotate: %s / %s / %s", m4Smoke.placed ? "OK" : "MISMATCH", m4Smoke.dragMoved ? "OK" : "MISMATCH",
                      m4Smoke.rotateApplied ? "OK" : "MISMATCH");
    ImGui::BulletText("core's own bake reflects the move / delete: %s / %s", m4Smoke.bakedRegionMoved ? "OK" : "MISMATCH",
                      m4Smoke.deleted ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("M5 smoke check (rail-edge toggle, exercised directly):");
    ImGui::BulletText("toggle flips shared asset edge / sets selection: %s / %s", m5Smoke.toggled ? "OK" : "MISMATCH",
                      m5Smoke.selectionSet ? "OK" : "MISMATCH");
    ImGui::BulletText("undo restores / redo reapplies: %s / %s", m5Smoke.undone ? "OK" : "MISMATCH", m5Smoke.redone ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("M6 smoke check (elevation drag, exercised directly):");
    ImGui::BulletText("elevation drag / undo / redo: %s / %s / %s", m6Smoke.elevationChanged ? "OK" : "MISMATCH",
                      m6Smoke.undone ? "OK" : "MISMATCH", m6Smoke.redone ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("M7a smoke check (random-track bake + USD export, exercised directly):");
    ImGui::BulletText("random track bakes: %s (%zu path(s), %zu geometry batch(es))", m7aSmoke.randomBakeOk ? "OK" : "FAILED",
                      m7aSmoke.randomPathCount, m7aSmoke.randomGeometryBatchCount);
    ImGui::BulletText("USD export header / meshes: %s / %s (%zu mesh(es))", m7aSmoke.usdHeaderOk ? "OK" : "MISMATCH",
                      m7aSmoke.usdHasMeshes ? "OK" : "MISMATCH", m7aSmoke.usdMeshCount);
    ImGui::TextUnformatted("MppModel smoke check (MPPMODEL_EXPORT_SPEC.md, exercised directly):");
    ImGui::BulletText("header / mesh count / fields match / byte sizes match / wide-index branch: %s / %s / %s / %s / %s",
                      mppModelSmoke.headerOk ? "OK" : "MISMATCH", mppModelSmoke.meshCountMatches ? "OK" : "MISMATCH",
                      mppModelSmoke.fieldsMatch ? "OK" : "MISMATCH", mppModelSmoke.byteSizesMatch ? "OK" : "MISMATCH",
                      mppModelSmoke.wideIndexChosenForLargeMesh ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("M7b smoke check (texture assets, exercised directly):");
    ImGui::BulletText("image size read / add asset / assign: %s / %s / %s", m7bSmoke.imageSizeReadOk ? "OK" : "MISMATCH",
                      m7bSmoke.assetAdded ? "OK" : "MISMATCH", m7bSmoke.assigned ? "OK" : "MISMATCH");
    ImGui::BulletText("tile resize keeps valid binding / clears invalid one / delete: %s / %s / %s",
                      m7bSmoke.tileResizeOk ? "OK" : "MISMATCH", m7bSmoke.invalidAssignmentCleared ? "OK" : "MISMATCH",
                      m7bSmoke.deleted ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("M7c smoke check (mesh-section random-track generation, exercised directly):");
    ImGui::BulletText("found a mesh-section seed / bakes cleanly: %s / %s (%zu path(s), %zu mesh asset(s), %zu placement(s), %zu warning(s))",
                      m7cSmoke.foundMeshSectionSeed ? "OK" : "FAILED", m7cSmoke.bakeOk ? "OK" : "FAILED", m7cSmoke.pathCount,
                      m7cSmoke.meshAssetCount, m7cSmoke.meshPlacementCount, m7cSmoke.warningCount);
    ImGui::TextUnformatted("M9 smoke check (mesh JSON import, exercised directly):");
    ImGui::BulletText("parse / import / rails boundary by default: %s / %s / %s", m9Smoke.parsedFromJson ? "OK" : "MISMATCH",
                      m9Smoke.imported ? "OK" : "MISMATCH", m9Smoke.railedBoundary ? "OK" : "MISMATCH");
    ImGui::BulletText("bakes cleanly / rejects non-mesh JSON: %s / %s", m9Smoke.bakesCleanly ? "OK" : "MISMATCH",
                      m9Smoke.badJsonRejected ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Parity-fix smoke check (EDITOR_PARITY_FIXES.md findings 1/4/5, exercised directly):");
    ImGui::BulletText("no id collision on Create / drawn path bakes as drawn: %s / %s",
                      paritySmoke.noIdCollisionOnCreate ? "OK" : "MISMATCH", paritySmoke.drawnPathBakesAsDrawn ? "OK" : "MISMATCH");
    ImGui::BulletText("start point preserved / clamped in range on delete: %s / %s",
                      paritySmoke.startPointPreservedOnDelete ? "OK" : "MISMATCH", paritySmoke.startClampedInRange ? "OK" : "MISMATCH");
    ImGui::BulletText("orphaned mesh asset pruned on export: %s", paritySmoke.orphanedMeshAssetPruned ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap1 smoke check (roll/width/crossSection point editing, exercised directly):");
    ImGui::BulletText("add roll/width/crossSection / edit fields / delete: %s/%s/%s / %s / %s", gap1Smoke.rollAdded ? "OK" : "MISMATCH",
                      gap1Smoke.widthAdded ? "OK" : "MISMATCH", gap1Smoke.crossSectionAdded ? "OK" : "MISMATCH",
                      gap1Smoke.fieldsEdited ? "OK" : "MISMATCH", gap1Smoke.deleted ? "OK" : "MISMATCH");
    ImGui::BulletText("baked roll/width reflect the edit: %s / %s", gap1Smoke.bakedRollApplied ? "OK" : "MISMATCH",
                      gap1Smoke.bakedWidthApplied ? "OK" : "MISMATCH");
    ImGui::BulletText("4-position floor holds / aux points unguarded by it: %s / %s",
                      gap1Smoke.deletingBelowFourPositionsRefused ? "OK" : "MISMATCH", gap1Smoke.deletingAuxPointsUnguarded ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap2 smoke check (track name editing, exercised directly):");
    ImGui::BulletText("rename / undo / redo / same-name no-op refused: %s / %s / %s / %s", gap2Smoke.renamed ? "OK" : "MISMATCH",
                      gap2Smoke.undone ? "OK" : "MISMATCH", gap2Smoke.redone ? "OK" : "MISMATCH", gap2Smoke.noOpRefused ? "OK" : "MISMATCH");
    ImGui::BulletText("empty name stays live in memory / falls back only on serialize: %s / %s",
                      gap2Smoke.emptyNameLiveInMemory ? "OK" : "MISMATCH", gap2Smoke.emptyNameFallsBackOnSerialize ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap3 smoke check (zones, exercised directly):");
    ImGui::BulletText("add / select / baked as path zone / baked factor default: %s / %s / %s / %s", gap3Smoke.added ? "OK" : "MISMATCH",
                      gap3Smoke.selected ? "OK" : "MISMATCH", gap3Smoke.bakedAsPathZone ? "OK" : "MISMATCH",
                      gap3Smoke.bakedFactorApplied ? "OK" : "MISMATCH");
    ImGui::BulletText("edit fields / add start-grid zone / bakes with both / delete: %s / %s / %s / %s", gap3Smoke.edited ? "OK" : "MISMATCH",
                      gap3Smoke.startGridAdded ? "OK" : "MISMATCH", gap3Smoke.bakesWithMultipleZones ? "OK" : "MISMATCH",
                      gap3Smoke.deleted ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap4 smoke check (triggers, exercised directly):");
    ImGui::BulletText("add / select / baked as gate / edit fields: %s / %s / %s / %s", gap4Smoke.added ? "OK" : "MISMATCH",
                      gap4Smoke.selected ? "OK" : "MISMATCH", gap4Smoke.bakedAsGate ? "OK" : "MISMATCH",
                      gap4Smoke.edited ? "OK" : "MISMATCH");
    ImGui::BulletText("second checkpoint / finish stays unique / delete blocked while finish / delete after demotion: %s / %s / %s / %s",
                      gap4Smoke.secondCheckpointAdded ? "OK" : "MISMATCH", gap4Smoke.finishUniqueAfterPromotion ? "OK" : "MISMATCH",
                      gap4Smoke.deleteBlockedWhileFinish ? "OK" : "MISMATCH", gap4Smoke.deletedAfterDemotion ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap5 smoke check (curve management, exercised directly):");
    ImGui::BulletText("default current path / clamps with one path: %s / %s", gap5Smoke.defaultCurrentPathIsZero ? "OK" : "MISMATCH",
                      gap5Smoke.clampsWithOnePath ? "OK" : "MISMATCH");
    ImGui::BulletText("closed-path disjoint / bakes open / reconnect: %s / %s / %s", gap5Smoke.closedMadeDisjoint ? "OK" : "MISMATCH",
                      gap5Smoke.closedBakesOpen ? "OK" : "MISMATCH", gap5Smoke.closedReconnected ? "OK" : "MISMATCH");
    ImGui::BulletText("open-path split / bakes as two paths / delete prunes dangling seam: %s / %s / %s",
                      gap5Smoke.openSplitDisjoint ? "OK" : "MISMATCH", gap5Smoke.openSplitBakes ? "OK" : "MISMATCH",
                      gap5Smoke.deleteCurrentPathPrunesDanglingSeam ? "OK" : "MISMATCH");
    ImGui::BulletText("join same-path closes / join cross-path junction / bakes: %s / %s / %s",
                      gap5Smoke.joinedSamePathCloses ? "OK" : "MISMATCH", gap5Smoke.joinedCrossPathCreatesJunction ? "OK" : "MISMATCH",
                      gap5Smoke.joinedCrossPathBakes ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap6 smoke check (direction toggle / start point, exercised directly):");
    ImGui::BulletText("toggle / undo / redo: %s / %s / %s", gap6Smoke.toggled ? "OK" : "MISMATCH",
                      gap6Smoke.toggleUndone ? "OK" : "MISMATCH", gap6Smoke.toggleRedone ? "OK" : "MISMATCH");
    ImGui::BulletText("set start point / no-op when already start / undo: %s / %s / %s", gap6Smoke.startMoved ? "OK" : "MISMATCH",
                      gap6Smoke.startMoveNoOpWhenAlreadyStart ? "OK" : "MISMATCH", gap6Smoke.startMoveUndone ? "OK" : "MISMATCH");
    ImGui::BulletText("baked starting-grid forward direction actually flips: %s", gap6Smoke.bakedGridReversed ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap7 smoke check (handling panel, exercised directly):");
    ImGui::BulletText("edit / out-of-range clamp / undo / redo: %s / %s / %s / %s", gap7Smoke.edited ? "OK" : "MISMATCH",
                      gap7Smoke.clamped ? "OK" : "MISMATCH", gap7Smoke.undone ? "OK" : "MISMATCH", gap7Smoke.redone ? "OK" : "MISMATCH");
    ImGui::BulletText("reset to default / baked handling matches edited value: %s / %s", gap7Smoke.reset ? "OK" : "MISMATCH",
                      gap7Smoke.bakedHandlingMatches ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap8 smoke check (random ranges panel, exercised directly):");
    ImGui::BulletText("custom turn-count range respected / default ranges still bake: %s / %s",
                      gap8Smoke.customTurnCountRespected ? "OK" : "MISMATCH", gap8Smoke.defaultRangesStillBake ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap10 smoke check (render modes / point filters / physics overlay, exercised directly):");
    ImGui::BulletText("default banked / render mode round-trips / filters default shown / position filter toggles: %s / %s / %s / %s",
                      gap10Smoke.defaultRenderModeIsBanked ? "OK" : "MISMATCH", gap10Smoke.renderModeRoundTrips ? "OK" : "MISMATCH",
                      gap10Smoke.pointFiltersDefaultShown ? "OK" : "MISMATCH", gap10Smoke.positionFilterToggles ? "OK" : "MISMATCH");
    ImGui::BulletText("physics points hidden by default / selection round-trips / hiding clears selection: %s / %s / %s",
                      gap10Smoke.physicsPointsHiddenByDefault ? "OK" : "MISMATCH", gap10Smoke.physicsSelectionRoundTrips ? "OK" : "MISMATCH",
                      gap10Smoke.hidingPhysicsClearsSelection ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap11 smoke check (segment select/delete/split, insert-on-segment, exercised directly):");
    ImGui::BulletText("outgoing nullopt on aux selection / at open-path end / incoming nullopt at open-path start: %s / %s / %s",
                      gap11Smoke.outgoingNulloptOnAuxSelection ? "OK" : "MISMATCH", gap11Smoke.outgoingNulloptAtOpenPathEnd ? "OK" : "MISMATCH",
                      gap11Smoke.incomingNulloptAtOpenPathStart ? "OK" : "MISMATCH");
    ImGui::BulletText("closed-path delete opens path / keeps all points: %s / %s", gap11Smoke.closedSegmentDeleteOpensPath ? "OK" : "MISMATCH",
                      gap11Smoke.closedSegmentDeleteKeepsAllPoints ? "OK" : "MISMATCH");
    ImGui::BulletText("open first-segment shrinks / floor guard holds: %s / %s", gap11Smoke.openFirstSegmentShrinks ? "OK" : "MISMATCH",
                      gap11Smoke.openFloorGuardHolds ? "OK" : "MISMATCH");
    ImGui::BulletText("open middle-segment splits / split guard holds / disjoint-seam guard holds: %s / %s / %s",
                      gap11Smoke.openMiddleSegmentSplits ? "OK" : "MISMATCH", gap11Smoke.openMiddleSplitGuardHolds ? "OK" : "MISMATCH",
                      gap11Smoke.disjointSeamGuardHolds ? "OK" : "MISMATCH");
    ImGui::BulletText("insert-on-segment adds point / bakes: %s / %s", gap11Smoke.insertOnSegmentAddsPoint ? "OK" : "MISMATCH",
                      gap11Smoke.insertedPointBakes ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap9 smoke check (grid display / size / snap, exercised directly):");
    ImGui::BulletText("no snap by default / snaps once shown+enabled / hidden grid disables snap: %s / %s / %s",
                      gap9Smoke.noSnapByDefault ? "OK" : "MISMATCH", gap9Smoke.snapOnlyWhenGridShownAndSnapEnabled ? "OK" : "MISMATCH",
                      gap9Smoke.hiddenGridDisablesSnap ? "OK" : "MISMATCH");
    ImGui::BulletText("respects configured grid size / create-click snaps new point / closing stays unsnapped: %s / %s / %s",
                      gap9Smoke.respectsGridSize ? "OK" : "MISMATCH", gap9Smoke.createClickSnapsNewPoint ? "OK" : "MISMATCH",
                      gap9Smoke.createClickClosingStaysUnsnapped ? "OK" : "MISMATCH");
    ImGui::TextUnformatted("Gap14 smoke check (undo/redo disabled state, exercised directly):");
    ImGui::BulletText("empty at start / undo enabled after edit / redo disabled after edit: %s / %s / %s",
                      gap14Smoke.emptyAtStart ? "OK" : "MISMATCH", gap14Smoke.undoEnabledAfterEdit ? "OK" : "MISMATCH",
                      gap14Smoke.redoDisabledAfterEdit ? "OK" : "MISMATCH");
    ImGui::BulletText("redo enabled after undo / undo disabled once exhausted: %s / %s", gap14Smoke.redoEnabledAfterUndo ? "OK" : "MISMATCH",
                      gap14Smoke.undoDisabledAfterUndoingEverything ? "OK" : "MISMATCH");
      ImGui::PopID();
    }
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    ImGui::Begin("Top-Down View");
    if (editor::DrawTopDownCanvas(topDownView, editorState, bakedTrack)) rebake();
    ImGui::End();

    // Mirrors editor.js's elevCollapsed: the panel can be hidden (its own persisted preference in
    // the web editor; a plain in-session toggle here, since there's no settings file yet).
    ImGui::SetNextWindowSize(ImVec2(900, 260), ImGuiCond_FirstUseEver);
    ImGui::Begin("Elevation Profile");
    ImGui::Checkbox("Show", &elevationVisible);
    if (elevationVisible) {
      ImGui::Separator();
      // Mirrors editor.js's curPath(): the currently selected point's path if there is one,
      // otherwise EditorState::currentPathIndex()'s own fallback (the curve-selector dropdown's
      // choice, or path 0 -- EDITOR_PARITY_FIXES.md gap 5).
      const int elevationPathIndex = editorState.track().paths.empty() ? -1 : editorState.currentPathIndex();
      if (editor::DrawElevationView(editorState, bakedTrack, elevationPathIndex)) rebake();
    }
    ImGui::End();

    ImGui::Render();
    int drawableWidth = 0, drawableHeight = 0;
    SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
    glViewport(0, 0, drawableWidth, drawableHeight);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
