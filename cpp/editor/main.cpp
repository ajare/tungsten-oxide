// cpp/editor/main.cpp — track_editor: native ImGui/SDL3/OpenGL track editor.
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
// current path's baked Y profile plus draggable position-point elevation markers, collapsible --
// retired by DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 1.4: Front canvas projection mode (Milestone
// 1.2's generalized drag-to-move, applied to TopDownCanvas.cpp's existing position-point handling)
// takes over height editing, dragging a point's (x, y) directly instead of a separate panel.
// M7a adds USD export (USDExport.hpp/.cpp, walking core's own baked renderer-neutral
// tox::Track::geometry batches into .usda Mesh prims -- not a from-scratch surface derivation, see
// USDExport.hpp) and random-track generation (RandomTrack.hpp/.cpp,
// initially scoped to the closed-loop/no-mesh-sections branch only). M7b adds texture
// assets: TextureCache.hpp/.cpp decodes PNGs with the vendored stb_image and uploads GL textures
// for thumbnails; TexturePanel.hpp/.cpp is the asset list + tile-grid picker UI, backed by
// EditorState's new addTextureAsset/deleteTextureAsset/setTextureTileSize/assignPathTexture/
// clearPathTexture. M7c completes RandomTrack.hpp/.cpp with the mesh-section branch: splitting the
// loop into open ordinary paths joined by generated jump platforms/ramps, with an iterative
// endpoint-blend solve (via probe bakes through core, not a reimplemented spline evaluator -- see
// RandomTrack.hpp) to land each drop exactly. M8 (EDITOR_NATIVE_FILE_IO_PLAN.md) adds New/Export
// JSON/Export USD/Import JSON, backed by FileDialog.hpp/.cpp's modern IFileOpenDialog/
// IFileSaveDialog wrappers. M9 adds mesh asset import: EditorTrackDefinition.hpp's
// parseMeshAssetJson (reusing
// its existing file-local normalizeMeshAsset -- no new from-scratch parser needed after all) plus
// EditorState::importMeshAsset/importMeshFromJsonText back the toolbar's Import/Paste Mesh buttons
// and TopDownCanvas.cpp's new minimal right-click "Paste Mesh" context menu; Clipboard.hpp/.cpp
// wraps CF_UNICODETEXT for the paste path. M10 adds TexturePanel.cpp's "Browse..." button next to
// "Load Bundled Textures", reusing M7b's readImageSize/addTextureAsset with FileDialog.hpp's
// Open dialog -- almost entirely wiring.
// DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2 later removed everything M4/M5/M9 added above (mesh
// region placement/drag/rotate, Rails mode, mesh JSON import/paste) along with MeshRegion itself;
// M6/M7/M8/M10's own work is unaffected.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

#include "imconfig.h"  // pulls in the vendored gl3w loader (see imconfig.h)
#include "imgui.h"
#include "imgui_internal.h"  // ImGui::DockBuilder* -- used once at startup to build the fixed layout
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#include <SDL3/SDL.h>

#include "Clipboard.hpp"
#include "EditorHistory.hpp"
#include "EditorIni.hpp"
#include "EditorState.hpp"
#include "EditorTrackDefinition.hpp"
#include "Track.hpp"
#include "FileDialog.hpp"
#include "fontawesome/IconsFontAwesome5.h"
#include "MaterialCatalog.hpp"
#include "MaterialsPanel.hpp"
#include "ModelsPanel.hpp"
#include "ModelPlacementsPanel.hpp"
#include "ModelXml.hpp"
#include "RandomTrack.hpp"
#include "StartGrid.hpp"
#include "TextureCache.hpp"
#include "HandlingPanel.hpp"
#include "RandomRangesPanel.hpp"
#include "PropertiesPanel.hpp"
#include "TrackPropertiesPanel.hpp"
#include "ZonesPanel.hpp"
#include "TriggersPanel.hpp"
#include "ReservationsPanel.hpp"
#include "CurvesPanel.hpp"
#include "TopDownCanvas.hpp"
#include "TopDownView.hpp"
#include "TrackResourceDocument.hpp"
#include "TrackResourceSave.hpp"
#include "USDExport.hpp"
#include "MppModelExport.hpp"

namespace {

// Shared by the View menu's "Render Mode" submenu and the toolbar's render-mode combobox, so the
// two pickers list the same modes in the same order and can't drift apart.
const std::pair<const char*, editor::TopDownView::RenderMode> kRenderModes[] = {
    {"Banked edges (lean tint)", editor::TopDownView::RenderMode::Banked},
    {"Flat width (roll colour)", editor::TopDownView::RenderMode::Flat},
    {"Flat with elevation colour", editor::TopDownView::RenderMode::Elevation},
    {"Flat with camber colour", editor::TopDownView::RenderMode::Camber},
};

// Default export filename stem, sanitized: ASCII word chars only, so runs of anything else
// collapse to a single underscore.
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
// non-ASCII track name in the Save dialog's default filename.
std::wstring toWide(const std::string& text) { return editor::utf8ToWide(text); }

// A flat 8km circle (12 control points, calibrated radius, roll/width/crossSection defaults, a
// finish + 3 intermediate checkpoints) -- there is no "new track" UI yet, so this is the only
// in-memory content M1 has to exercise the authoring model against.
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
  // Not json1 == json2: buildStarterTrack() constructs points with no id at all, so json1's ids
  // are legitimately empty and json2's are legitimately p1..p12 once fromJson's backfilling has
  // run. That first parse is where backfilling happens; it is not
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
  // dragSelectedTo rounds to 0.1m, so the expected
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

// M4/M5 smoke checks (mesh placement select/drag/rotate/delete, rail-edge toggling) were removed
// along with MeshRegion/MeshAsset/MeshPlacement (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2).

// M6 smoke check, updated for DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 1.4: ElevationView is
// retired, so height editing now goes through Front canvas projection mode's generalized
// dragSelectedTo (Milestone 1.2) instead of the old Y-only dragSelectedElevationTo -- this proves
// out end to end that Front mode reaches position-point height editing, the one capability
// ElevationView provided that nothing else covered (Front's plane is (x, y), so the drag also
// carries x through unchanged here to confirm the third axis, z, is the only one left alone).
// Milestone 1.2's follow-up fix negates the plane's Y slot (EditorState::setPlaneCoords) so
// dragging "up" on screen raises Y instead of lowering it -- the drag's v argument below is
// therefore -(targetY), not targetY.
struct M6SmokeCheckResult {
  bool elevationChanged = false, undone = false, redone = false;
};

M6SmokeCheckResult runM6SmokeCheck() {
  M6SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  const double originalX = state.track().paths[0].points[0].pos.x;
  const double originalY = state.track().paths[0].points[0].pos.y;
  const double originalZ = state.track().paths[0].points[0].pos.z;
  // dragSelectedTo rounds to a 0.1m boundary (same as every other on-canvas drag), so the expected
  // x has to go through the same rounding the starter track's non-0.1-aligned coordinates do.
  const double expectedX = std::round(originalX * 10.0) / 10.0;
  state.selectPoint(0, 0);
  state.setProjectionMode(editor::ProjectionMode::Front);
  state.beginDrag();
  state.dragSelectedTo(originalX, -(originalY + 25.0));  // Front plane: (x, -y)
  state.endDrag();
  // Captured as plain values, not a reference into track_: undo()/redo() below replace the whole
  // TrackDefinition, which would leave a reference dangling.
  const tox::Vec3 moved = state.track().paths[0].points[0].pos;
  // +25.0 already lands on a 0.1m boundary; z must be untouched by a Front-plane drag.
  result.elevationChanged = (moved.y == originalY + 25.0) && (moved.x == expectedX) && (moved.z == originalZ);

  result.undone = state.undo() && state.track().paths[0].points[0].pos.y == originalY;
  result.redone = state.redo() && state.track().paths[0].points[0].pos.y == moved.y;

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

// M7c's mesh-section-branch smoke check was removed: RandomTrack.cpp's mesh-section generation
// (splitting the loop into paths joined by placed-mesh-asset jump platforms) is gone along with
// MeshAsset/MeshPlacement (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2) -- generateRandomTrack now
// always takes the closed-loop/no-cuts branch M7a originally scoped this to.

// M7b smoke check: register a texture asset against one of the repo's real checked-in images
// (assets/test-1.png, stored as "../assets/test-1.png" -- see TextureCache.cpp's get()), assign it
// to the starter path, resize its tile grid, and confirm the
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
  const std::string assetId = state.addTextureAsset("test-1.png", "../assets/test-1.png", std::max(width, 1), std::max(height, 1));
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

// M9's mesh-JSON-import smoke check was removed along with parseMeshAssetJson/
// importMeshFromJsonText/MeshAsset (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2).

// Parity-fix smoke check: regression coverage for the fixes that silently corrupted or lost
// authored data rather than crashing or misformatting. A few related fixes are too
// UI/encoding-specific for a headless check.
struct ParitySmokeCheckResult {
  bool noIdCollisionOnCreate = false, drawnPathBakesAsDrawn = false;
  bool startPointPreservedOnDelete = false, startClampedInRange = false;
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

  // Finding 5 (an orphaned mesh asset must not survive export) was removed along with
  // MeshAsset/MeshPlacement (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 2).

  return result;
}

// Gap-1 smoke check: add/edit/delete a roll, width,
// and cross-section point through EditorState directly (the same methods PropertiesPanel.cpp
// calls), and confirm core's own bake reflects a banked/widened/curved cross-section -- not just
// that the schema round-trips, but that the values actually reach the physics.
struct Gap1SmokeCheckResult {
  bool rollAdded = false, widthAdded = false, crossSectionAdded = false;
  bool fieldsEdited = false, deleted = false;
  bool bakedRollApplied = false, bakedWidthApplied = false;
  bool deletingBelowFourPositionsRefused = false, deletingAuxPointsUnguarded = false;
  bool selectionIsPositionTrueForPosition = false, selectionIsPositionFalseForAux = false, selectionIsPositionFalseWhenInvalid = false;
  bool selectionIsWidthTrueForWidth = false, selectionIsWidthFalseForPosition = false;
  bool widthDragged = false, widthDragClampsToFloor = false, widthDragUndone = false, widthDragRefusedForPositionSelection = false;
  bool selectionIsRollTrueForRoll = false, selectionIsRollFalseForPosition = false;
  bool rollDragged = false, rollDragClampsToRange = false, rollDragUndone = false, rollDragRefusedForPositionSelection = false;
};

Gap1SmokeCheckResult runGap1SmokeCheck() {
  Gap1SmokeCheckResult result;

  editor::EditorState state(buildStarterTrack());
  const auto rollIndex = state.addAuxPoint(0, editor::PointKind::Roll, 0.1);
  result.rollAdded = rollIndex.has_value() && state.selection().pathIndex == 0 && state.selection().pointIndex == *rollIndex;
  // addAuxPoint() selects the point it just added -- selectionIsPosition() (whose on-canvas handles
  // support click-to-select but no on-canvas drag) must say false for it, and
  // true once a real Position point is selected instead, so TopDownCanvas.cpp's drag-continuation
  // guard can tell them apart.
  result.selectionIsPositionFalseForAux = !state.selectionIsPosition();
  const auto widthIndex = state.addAuxPoint(0, editor::PointKind::Width, 0.1);
  result.widthAdded = widthIndex.has_value();
  const auto crossSectionIndex = state.addAuxPoint(0, editor::PointKind::CrossSection, 0.1);
  result.crossSectionAdded = crossSectionIndex.has_value();
  result.selectionIsPositionFalseWhenInvalid = (state.clearSelection(), !state.selectionIsPosition());
  // Captured by value, not reference: state.undo()/redo() below replace track_ wholesale
  // (replaceTrackKeepHistory's move-assignment), which would dangle a reference into the old one.
  const double firstPositionX = state.track().paths[0].points[0].pos.x, firstPositionZ = state.track().paths[0].points[0].pos.z;
  state.selectPositionAt(firstPositionX, firstPositionZ, 1.0);
  result.selectionIsPositionTrueForPosition = state.selectionIsPosition();
  result.selectionIsWidthFalseForPosition = !state.selectionIsWidth();
  result.selectionIsRollFalseForPosition = !state.selectionIsRoll();

  result.fieldsEdited = state.editAuxPoint(0, *rollIndex, [](editor::TrackPoint& p) { p.roll = 25.0; }) &&
                        state.track().paths[0].points[*rollIndex].roll == 25.0;
  result.fieldsEdited = result.fieldsEdited &&
                        state.editAuxPoint(0, *widthIndex, [](editor::TrackPoint& p) { p.width = 60.0; }) &&
                        state.track().paths[0].points[*widthIndex].width == 60.0;

  // On-canvas width-handle drag: EditorState's
  // side of it only (dragSelectedWidthTo's screen-position -> width math lives in
  // TopDownCanvas.cpp, ImGui-adjacent glue with no headless entry point -- same tradeoff already
  // taken for gap 8's sanitize()/gap 13's nearestPathPlacement()).
  state.selectPoint(0, *widthIndex);
  result.selectionIsWidthTrueForWidth = state.selectionIsWidth();
  state.beginDrag();
  state.dragSelectedWidthTo(75.0);
  result.widthDragged = state.track().paths[0].points[*widthIndex].width == 75.0;
  state.dragSelectedWidthTo(-40.0);  // must clamp to the 1.0 floor, mirroring editAuxPoint's own clamp
  result.widthDragClampsToFloor = state.track().paths[0].points[*widthIndex].width == 1.0;
  state.endDrag();
  result.widthDragUndone = state.undo() && state.track().paths[0].points[*widthIndex].width == 60.0;

  // Dragging via dragSelectedWidthTo() while a POSITION point is selected must be a no-op --
  // mirrors selectionIsPosition's own guard note: dragSelectedTo/dragSelectedWidthTo only ever
  // touch the field their own point kind uses.
  state.selectPositionAt(firstPositionX, firstPositionZ, 1.0);
  const double widthBeforeMisdirectedDrag = state.track().paths[0].points[*widthIndex].width;
  state.beginDrag();
  state.dragSelectedWidthTo(999.0);
  state.endDrag();
  result.widthDragRefusedForPositionSelection = state.track().paths[0].points[*widthIndex].width == widthBeforeMisdirectedDrag;

  // On-canvas roll-handle drag: same split as the
  // width-drag block above -- EditorState's clamp/undo bookkeeping only, screen-position -> roll
  // math lives in TopDownCanvas.cpp.
  state.selectPoint(0, *rollIndex);
  result.selectionIsRollTrueForRoll = state.selectionIsRoll();
  state.beginDrag();
  state.dragSelectedRollTo(90.0);
  result.rollDragged = state.track().paths[0].points[*rollIndex].roll == 90.0;
  state.dragSelectedRollTo(250.0);  // must clamp to the 180 ceiling, mirroring editAuxPoint's own clamp
  result.rollDragClampsToRange = state.track().paths[0].points[*rollIndex].roll == 180.0;
  state.endDrag();
  result.rollDragUndone = state.undo() && state.track().paths[0].points[*rollIndex].roll == 25.0;

  // Dragging via dragSelectedRollTo() while a POSITION point is selected must be a no-op, same as
  // the width-drag guard above.
  state.selectPositionAt(firstPositionX, firstPositionZ, 1.0);
  const double rollBeforeMisdirectedDrag = state.track().paths[0].points[*rollIndex].roll;
  state.beginDrag();
  state.dragSelectedRollTo(-999.0);
  state.endDrag();
  result.rollDragRefusedForPositionSelection = state.track().paths[0].points[*rollIndex].roll == rollBeforeMisdirectedDrag;

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

// Gap-2 smoke check: rename the track, undo/redo it,
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

// Gap-3 smoke check: add a path-hosted boost zone,
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

// Gap-4 smoke check: add a path-hosted checkpoint
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

  // buildStarterTrack() already seeds four checkpoint triggers (one of them "finish"), so every
  // check below must match by id, never by index.
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

// Gap-5 smoke check (curve management): exercises
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

// Gap-6 smoke check: direction toggle and
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

// Gap-7 smoke check: the handling panel
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

// Gap-9 smoke check: top-down grid display, grid
// size, and snap-to-grid. Exercises TopDownView::snapWorldXZ directly (no ImGui needed -- it's pure view
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

// Gap-14 smoke check: undo/redo disabled state -- disabled while their stack is empty rather than
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

// Gap-8 smoke check: the random-ranges panel.
// `generateRandomTrack` already accepted a `RandomTrackRanges` parameter (M7a/M7c); main.cpp
// simply never passed anything but the `{}` default until this gap's UI wiring. Confirms a custom
// range actually reaches the generator -- pinning turnsMin == turnsMax forces the generator's
// control-point count to that exact value regardless of complexity (`n` in generateRandomTrack
// collapses to `turnsMin` when the range has zero width) -- and that the default-constructed
// ranges still bake cleanly (regression check: this is what every prior random-track call
// implicitly used). The panel's own field-clamping (`sanitize`, a direct port of
// sanitizeRandomRanges) is UI-only logic exercised through ImGui widgets with no headless entry
// point, consistent with how the other gap panels (Zones/Triggers/Properties) aren't unit-tested
// directly either -- only the EditorState/generator side each one drives is.
struct Gap8SmokeCheckResult {
  bool customTurnCountRespected = false, defaultRangesStillBake = false;
};

Gap8SmokeCheckResult runGap8SmokeCheck() {
  Gap8SmokeCheckResult result;

  editor::RandomTrackRanges fixedTurns;
  fixedTurns.turnsMin = fixedTurns.turnsMax = 9;
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
// name/primitive-type/primitive-count/material/vertex-count/stride/non-indexed sentinel/data-size.
struct MppModelReadResult {
  bool ok = false;
  std::string error;
  std::uint32_t versionMajor = 0, versionMinor = 0, flags = 0;
  struct Mesh {
    std::string name, material;
    std::uint32_t primitiveType = 0, primitiveCount = 0;
    std::uint32_t vertexCount = 0, vertexStride = 0, vertexDataSize = 0;
    std::uint32_t indexStreamId = 0, indexWidth = 0, indexDataSize = 0;
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

  if (!need(12) || bytes.compare(0, 4, "MPPM") != 0) {
    result.error = "bad header/magic";
    return result;
  }
  pos = 4;
  result.versionMajor = u16();
  result.versionMinor = u16();
  result.flags = u32();

  struct Entry {
    std::uint32_t type, start, end, count;
  };
  Entry entries[6];
  for (auto& e : entries) {
    if (!need(16)) {
      result.error = "truncated directory";
      return result;
    }
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
    if (numVertexBuffers != 1) {
      result.error = "expected exactly one vertex buffer per mesh";
      return result;
    }
    const std::uint32_t vertexStreamId = u32();
    const std::uint32_t indexStreamId = u32();
    streamIds.push_back({vertexStreamId, indexStreamId});
    mesh.indexStreamId = indexStreamId;
    result.meshes.push_back(mesh);
  }
  if (pos != meshDir.end) {
    result.error = "mesh metadata section size mismatch";
    return result;
  }

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
  if (pos != vertexDir.end) {
    result.error = "vertex data section size mismatch";
    return result;
  }

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
  if (pos != indexDir.end) {
    result.error = "index data section size mismatch";
    return result;
  }

  result.ok = true;
  return result;
}

struct MppModelSmokeCheckResult {
  bool headerOk = false, meshCountMatches = false, fieldsMatch = false, byteSizesMatch = false,
       nonIndexedTriangleSoup = false;
};

MppModelSmokeCheckResult runMppModelSmokeCheck() {
  MppModelSmokeCheckResult result;

  const tox::TrackLoadResult baked = tox::Track::fromJson(editor::toJson(buildStarterTrack()));
  if (!baked) return result;

  const editor::MppModelExportResult exported = editor::exportTrackToMppModel(*baked.track);
  const MppModelReadResult read = readMppModelStructurally(exported.bytes);
  if (!read.ok) return result;

  result.headerOk = read.versionMajor == 1 && read.versionMinor == 1 && read.flags == 0x0000;
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
    if (mesh.indexStreamId != 0xFFFFFFFFu || mesh.indexWidth != 0 || mesh.indexDataSize != 0)
      fieldsMatch = false;
  }
  result.fieldsMatch = fieldsMatch;
  result.byteSizesMatch = byteSizesMatch;

  // Even a >65535-vertex batch remains a non-indexed triangle soup; there is no redundant
  // identity index stream and therefore no 16/32-bit width branch.
  tox::GeometryBatch wideBatch;
  wideBatch.id = "wide-test";
  wideBatch.materialKey = "road";
  wideBatch.vertices.resize(70000);
  wideBatch.indices = {0, 1, 2};
  tox::Track wideTrack;
  wideTrack.geometry.push_back(wideBatch);
  const editor::MppModelExportResult wideExported = editor::exportTrackToMppModel(wideTrack);
  const MppModelReadResult wideRead = readMppModelStructurally(wideExported.bytes);
  result.nonIndexedTriangleSoup = wideRead.ok && !wideRead.meshes.empty() &&
                                  wideRead.meshes[0].indexStreamId == 0xFFFFFFFFu &&
                                  wideRead.meshes[0].indexWidth == 0;

  return result;
}

// LoadModel smoke check (TRACK_MODEL_LIST_PLAN.md Milestone 6): loading the same Model twice
// reuses the embedded entry (dedup by ModelFile, per the locked-in decision) and only adds a new
// placement; loading a different Model embeds a second entry; editing an embedded Model's mesh
// metadata is visible through every placement that references it, since the metadata belongs to
// the shared Model entry, not any one placement.
struct LoadModelSmokeCheckResult {
  bool embedCreatesNoPlacement = false;
  bool firstPlacementCreatesInstance = false;
  bool secondPlacementReusesEmbeddedModel = false;
  bool differentModelEmbedsSecondEntry = false;
  bool editEmbeddedModelAffectsSharedMetadata = false;
};

LoadModelSmokeCheckResult runLoadModelSmokeCheck() {
  LoadModelSmokeCheckResult result;
  editor::EditorState state(buildStarterTrack());

  modelxml::ModelXmlDefinition cubeModel;
  cubeModel.meshes.push_back({"main", modelxml::MeshType::Physical, true});
  const std::string cubeModelId = state.embedModel(cubeModel, "Cube.mppmodel");
  result.embedCreatesNoPlacement = state.track().models.size() == 1 && state.track().meshObjects.empty();

  state.placeModelInstance(cubeModelId, 10.0, 20.0);
  result.firstPlacementCreatesInstance = state.track().models.size() == 1 && state.track().meshObjects.size() == 1 &&
                                         state.track().meshObjects[0].modelId == cubeModelId;

  const std::string reusedModelId = state.embedModel(cubeModel, "Cube.mppmodel");
  state.placeModelInstance(reusedModelId, 30.0, 40.0);
  result.secondPlacementReusesEmbeddedModel = reusedModelId == cubeModelId && state.track().models.size() == 1 &&
                                              state.track().meshObjects.size() == 2 &&
                                              state.track().meshObjects[0].modelId == state.track().meshObjects[1].modelId;

  modelxml::ModelXmlDefinition rampModel;
  rampModel.meshes.push_back({"ramp", modelxml::MeshType::Physical, true});
  const std::string rampModelId = state.embedModel(rampModel, "Ramp.mppmodel");
  state.placeModelInstance(rampModelId, 50.0, 60.0);
  result.differentModelEmbedsSecondEntry = state.track().models.size() == 2 && state.track().meshObjects.size() == 3;

  result.editEmbeddedModelAffectsSharedMetadata =
      state.editEmbeddedModel(cubeModelId, [](modelxml::ModelXmlDefinition& m) { m.meshes[0].visible = false; }) &&
      state.findModel(cubeModelId) != nullptr && !state.findModel(cubeModelId)->meshes[0].visible;

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

// Gap-11 smoke check: segment selection,
// deletion, splitting, and insert-point-on-segment: selectedOutgoingSegment/selectedIncomingSegment/
// insertNear. Deliberately does not exercise click-to-select-a-segment, which is never wired to
// any UI here (see EditorState.hpp's comment on the port).
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

// Gap-10 smoke check: render modes, point-type
// filters, and the physics-sample overlay. Exercises TopDownView's new state directly (pure view
// state, no ImGui needed -- same reasoning as gap 9's snapWorldXZ check); the render-mode fill
// color formulas (rollFillColor/elevationFillColor) and physicsPointAtWorld/
// drawPhysicsSampleInfo are ImGui-adjacent glue with no headless entry point -- same tradeoff
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

// Locates cpp/editor/resources/<filename> by walking up from the current working directory --
// mirrors TextureCache.hpp's findAssetsDir() for the same reason: the editor's build output
// directory (e.g. cpp/build/editor/Release) is nested several levels under the repo root, so a
// path relative to the CWD only resolves when the editor happens to be launched from there.
std::filesystem::path findEditorResourceFile(const std::string& filename) {
  std::filesystem::path dir = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    std::error_code ec;
    const std::filesystem::path candidate = dir / "cpp" / "editor" / "resources" / filename;
    if (std::filesystem::exists(candidate, ec)) return candidate;
    if (!dir.has_parent_path() || dir.parent_path() == dir) break;
    dir = dir.parent_path();
  }
  return {};
}

// Structural resource-loading failures (missing/empty editor.ini, an unresolvable Resources
// path, or a malformed Resources.xml) are treated as fatal -- the editor tears down whatever
// SDL/ImGui state it already created and exits, rather than silently running with no material
// catalog. A single TrackMaterial with a broken dependency chain is NOT structural (see
// MaterialCatalog::load) and does not reach this path.
[[noreturn]] void failStartup(SDL_Window* window, SDL_GLContext glContext, const std::string& message) {
  std::fprintf(stderr, "%s\n", message.c_str());
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DestroyContext(glContext);
  SDL_DestroyWindow(window);
  SDL_Quit();
  std::exit(1);
}

// editor.ini is a build artifact copied beside track_editor.exe (see the editor CMakeLists.txt
// POST_BUILD step), not a source-tree file -- unlike findEditorResourceFile() above (which
// intentionally walks up from the CWD to source-tree cpp/editor/resources/, since the
// FontAwesome font it locates is never copied to the output directory), this must resolve
// against the executable's own directory via SDL_GetBasePath() so a stray source-tree
// editor.ini can never shadow what was actually deployed alongside the running exe.
std::filesystem::path exeDirEditorIniPath() {
  const char* base = SDL_GetBasePath();
  if (base == nullptr) return {};

  return std::filesystem::path(base) / "editor.ini";
}

std::filesystem::path configuredMaterialResourcesPath() {
  const std::filesystem::path iniPath = exeDirEditorIniPath();
  if (iniPath.empty() || !std::filesystem::exists(iniPath))
    throw std::runtime_error("editor.ini not found beside the executable.");
  const editor::EditorIni ini = editor::EditorIni::load(iniPath);
  const std::optional<std::string> resourcesPath = ini.get("Resources", "Path");
  if (!resourcesPath.has_value() || resourcesPath->empty())
    throw std::runtime_error("editor.ini has no [Resources] Path entry.");
  return (iniPath.parent_path() / *resourcesPath).lexically_normal();
}

struct StartupMaterials {
  std::filesystem::path resourcesPath;
  editor::MaterialCatalog catalog;
};

// Startup loading is fatal; the same path is retained for the non-fatal runtime refresh command.
StartupMaterials loadMaterialCatalog(SDL_Window* window, SDL_GLContext glContext,
                                     editor::TextureCache& textureCache) {
  try {
    StartupMaterials result;
    result.resourcesPath = configuredMaterialResourcesPath();
    result.catalog = editor::MaterialCatalog::load(result.resourcesPath, textureCache);
    return result;
  } catch (const std::exception& e) {
    failStartup(window, glContext, std::string("Failed to load material resources: ") + e.what());
  }
}

int main(int, char**) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
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
      static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Window* window = SDL_CreateWindow("track_editor", 1280, 800, windowFlags);
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
    SDL_GL_DestroyContext(glContext);
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
  // filling the rest) is meant to be the SAME fixed arrangement every launch, not something that
  // drifts based on whatever a prior session happened to leave docked where.
  io.IniFilename = nullptr;

  ImGui::StyleColorsDark();

  // FontAwesome icons (Undo/Redo on the toolbar, more later): merged into the default font atlas
  // rather than loaded standalone, so ICON_FA_* glyphs can sit inline in ordinary button/text
  // labels at the default font's baseline/line height (the standard ImFontConfig::MergeMode
  // icon-font recipe -- see cpp/editor/include/fontawesome/README-VENDORED.md). Missing the font
  // file (e.g. a stripped-down checkout) just falls back to the default font with no icons, rather
  // than failing to start.
  constexpr float kBaseFontSize = 13.0f;  // ImGui's own default font size.
  // AddFontDefault() must be given an explicit SizePixels here: left implicit (the plain
  // AddFontDefault() call this used to be), this vendored ImGui's newer font-atlas code sets
  // ImFontFlags_ImplicitRefSize on the resulting font, and merging FontAwesome on top of it below
  // with an explicit SizePixels then trips
  // "Cannot use MergeMode with an explicit reference size when the destination font used an
  // implicit reference size!" in ImFontAtlas::AddFont (imgui_draw.cpp) -- an assert in Debug
  // builds, and silently wrong glyph scaling in Release.
  ImFontConfig defaultFontConfig;
  defaultFontConfig.SizePixels = kBaseFontSize;
  io.Fonts->AddFontDefault(&defaultFontConfig);
  const std::filesystem::path iconFontPath = findEditorResourceFile(FONT_ICON_FILE_NAME_FAS);
  if (!iconFontPath.empty()) {
    constexpr float kIconFontSize = kBaseFontSize * 2.0f / 3.0f;  // FontAwesome's own sizing recipe
    static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    iconConfig.GlyphMinAdvanceX = kIconFontSize;
    io.Fonts->AddFontFromFileTTF(editor::pathToUtf8(iconFontPath).c_str(), kIconFontSize, &iconConfig, iconRanges);
  }
  // No manual io.Fonts->Build() here: this vendored ImGui's OpenGL3/SDL3 backends use the newer
  // texture-management path (ImGuiBackendFlags_RendererHasTextures, set by ImGui_ImplOpenGL3_Init
  // below), which builds/uploads the atlas lazily on first use -- calling Build() before that flag
  // is set logs an imgui-error every frame.
  ImGui_ImplSDL3_InitForOpenGL(window, glContext);
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
  std::fprintf(stdout, "MppModel smoke check: header=%s meshCount=%s fields=%s byteSizes=%s nonIndexed=%s\n",
               mppModelSmoke.headerOk ? "OK" : "MISMATCH", mppModelSmoke.meshCountMatches ? "OK" : "MISMATCH",
               mppModelSmoke.fieldsMatch ? "OK" : "MISMATCH", mppModelSmoke.byteSizesMatch ? "OK" : "MISMATCH",
               mppModelSmoke.nonIndexedTriangleSoup ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const LoadModelSmokeCheckResult loadModelSmoke = runLoadModelSmokeCheck();
  std::fprintf(stdout, "LoadModel smoke check: embedNoPlacement=%s firstPlacement=%s dedupReuse=%s secondModelEmbeds=%s sharedMetadataEdit=%s\n",
               loadModelSmoke.embedCreatesNoPlacement ? "OK" : "MISMATCH", loadModelSmoke.firstPlacementCreatesInstance ? "OK" : "MISMATCH",
               loadModelSmoke.secondPlacementReusesEmbeddedModel ? "OK" : "MISMATCH",
               loadModelSmoke.differentModelEmbedsSecondEntry ? "OK" : "MISMATCH",
               loadModelSmoke.editEmbeddedModelAffectsSharedMetadata ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const M7bSmokeCheckResult m7bSmoke = runM7bSmokeCheck();
  std::fprintf(stdout, "M7b smoke check: imageSize=%s add=%s assign=%s tileResize=%s invalidClear=%s delete=%s\n",
               m7bSmoke.imageSizeReadOk ? "OK" : "MISMATCH", m7bSmoke.assetAdded ? "OK" : "MISMATCH", m7bSmoke.assigned ? "OK" : "MISMATCH",
               m7bSmoke.tileResizeOk ? "OK" : "MISMATCH", m7bSmoke.invalidAssignmentCleared ? "OK" : "MISMATCH",
               m7bSmoke.deleted ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const ParitySmokeCheckResult paritySmoke = runParitySmokeCheck();
  std::fprintf(stdout,
               "Parity-fix smoke check: noIdCollision=%s drawnPathBakesAsDrawn=%s startPreserved=%s startClamped=%s\n",
               paritySmoke.noIdCollisionOnCreate ? "OK" : "MISMATCH", paritySmoke.drawnPathBakesAsDrawn ? "OK" : "MISMATCH",
               paritySmoke.startPointPreservedOnDelete ? "OK" : "MISMATCH", paritySmoke.startClampedInRange ? "OK" : "MISMATCH");
  std::fflush(stdout);

  const Gap1SmokeCheckResult gap1Smoke = runGap1SmokeCheck();
  std::fprintf(stdout,
               "Gap1 smoke check (roll/width/crossSection editing): add=%s/%s/%s edit=%s delete=%s bakedRoll=%s bakedWidth=%s "
               "positionFloorHeld=%s auxUnguarded=%s selectionIsPosition=%s/%s/%s\n",
               gap1Smoke.rollAdded ? "OK" : "MISMATCH", gap1Smoke.widthAdded ? "OK" : "MISMATCH",
               gap1Smoke.crossSectionAdded ? "OK" : "MISMATCH", gap1Smoke.fieldsEdited ? "OK" : "MISMATCH",
               gap1Smoke.deleted ? "OK" : "MISMATCH", gap1Smoke.bakedRollApplied ? "OK" : "MISMATCH",
               gap1Smoke.bakedWidthApplied ? "OK" : "MISMATCH", gap1Smoke.deletingBelowFourPositionsRefused ? "OK" : "MISMATCH",
               gap1Smoke.deletingAuxPointsUnguarded ? "OK" : "MISMATCH", gap1Smoke.selectionIsPositionTrueForPosition ? "OK" : "MISMATCH",
               gap1Smoke.selectionIsPositionFalseForAux ? "OK" : "MISMATCH",
               gap1Smoke.selectionIsPositionFalseWhenInvalid ? "OK" : "MISMATCH");
  std::fprintf(stdout,
               "Gap1 width-drag smoke check: selectionIsWidth=%s/%s widthDragged=%s clampsToFloor=%s undone=%s "
               "refusedForPositionSelection=%s\n",
               gap1Smoke.selectionIsWidthTrueForWidth ? "OK" : "MISMATCH", gap1Smoke.selectionIsWidthFalseForPosition ? "OK" : "MISMATCH",
               gap1Smoke.widthDragged ? "OK" : "MISMATCH", gap1Smoke.widthDragClampsToFloor ? "OK" : "MISMATCH",
               gap1Smoke.widthDragUndone ? "OK" : "MISMATCH", gap1Smoke.widthDragRefusedForPositionSelection ? "OK" : "MISMATCH");
  std::fprintf(stdout,
               "Gap1 roll-drag smoke check: selectionIsRoll=%s/%s rollDragged=%s clampsToRange=%s undone=%s "
               "refusedForPositionSelection=%s\n",
               gap1Smoke.selectionIsRollTrueForRoll ? "OK" : "MISMATCH", gap1Smoke.selectionIsRollFalseForPosition ? "OK" : "MISMATCH",
               gap1Smoke.rollDragged ? "OK" : "MISMATCH", gap1Smoke.rollDragClampsToRange ? "OK" : "MISMATCH",
               gap1Smoke.rollDragUndone ? "OK" : "MISMATCH", gap1Smoke.rollDragRefusedForPositionSelection ? "OK" : "MISMATCH");
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
  editor::TextureCache textureCache;
  // Loaded once at startup (editor.ini -> Resources.xml -> editor-authored PBR material choices
  // and their preview textures, eager-loaded into textureCache above) and unloaded on exit when this local goes
  // out of scope at the end of main() -- same RAII lifetime as textureCache itself. Must happen
  // before the first bake below: setAvailableMaterials backfills the starter track's paths (still
  // material == "" at this point) to the alphabetically-first material choice, and that backfill has
  // to be visible in the very first tox::Track::fromJson bake, not just after the first rebake().
  StartupMaterials startupMaterials = loadMaterialCatalog(window, glContext, textureCache);
  const std::filesystem::path materialResourcesPath = std::move(startupMaterials.resourcesPath);
  editor::MaterialCatalog materialCatalog = std::move(startupMaterials.catalog);
  std::fprintf(stdout, "MaterialCatalog: %zu PBR track material choice(s) loaded\n", materialCatalog.materials().size());
  std::fflush(stdout);
  {
    std::vector<std::string> qualifiedNames;
    qualifiedNames.reserve(materialCatalog.materials().size());
    for (const auto& entry : materialCatalog.materials()) qualifiedNames.push_back(entry.qualifiedName);
    editorState.setAvailableMaterials(std::move(qualifiedNames));
  }
  tox::TrackLoadResult bakedResult = tox::Track::fromJson(editor::toJson(editorState.track()));
  const tox::Track* bakedTrack = bakedResult ? &*bakedResult.track : nullptr;
  // Self-intersection crossing detection is the one expensive
  // (O(N^2)) part of a bake; cachedCrossings holds the last result computed with it ON, reused
  // while a drag is in progress (see rebake() below).
  std::vector<tox::SelfIntersection> cachedCrossings = bakedResult.track.has_value() ? bakedResult.track->selfIntersections : std::vector<tox::SelfIntersection>{};
  editor::TopDownView topDownView;
  int randomSeed = 12345;
  int randomComplexity = 5;
  // Random-track generator ranges: a session-only generator preference (see RandomRangesPanel.hpp),
  // not track data, so it lives here rather than in EditorState/undo history.
  editor::RandomTrackRanges randomRanges;
  // Status bar (docked to the bottom of the window): the most recent message replaces whatever's
  // showing and is displayed for 3 seconds.
  // Deliberately a real wall clock (steady_clock), not ImGui::GetTime(): several showStatus()
  // calls below happen right after a blocking native file dialog (showOpenFileDialog/
  // showSaveFileDialog) returns, which pumps its own message loop for however long the user
  // takes and stalls ImGui::NewFrame()/io.DeltaTime for that whole span. GetTime() would only
  // catch up on the next frame, immediately consuming (or overshooting) the 3-second budget the
  // instant the dialog closed, so the message would show for far less than 3 seconds.
  std::string statusMessage;
  std::chrono::steady_clock::time_point statusExpiresAt{};
  auto showStatus = [&](std::string message) {
    statusMessage = std::move(message);
    statusExpiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  };
  // Idle status-bar content, shown whenever no timed showStatus() message is active: the three
  // point/zone/trigger selection slots are mutually exclusive (EditorState.hpp's own
  // selectXAt()/clearXSelection() calls all reset the other two), so checking them in this order
  // and returning on the first match never misses or double-reports a selection.
  auto describeSelection = [&]() -> std::string {
    if (const editor::SelectedPoint sel = editorState.selection(); sel.valid()) {
      const auto& point = editorState.track().paths[sel.pathIndex].points[sel.pointIndex];
      switch (point.kind) {
        case editor::PointKind::Position: return "Position point selected";
        case editor::PointKind::Roll: return "Roll point selected";
        case editor::PointKind::Width: return "Width point selected";
        case editor::PointKind::CrossSection: return "Cross-section point selected";
      }
    }
    if (const auto& zoneId = editorState.selectedZoneId(); zoneId.has_value()) {
      const editor::Zone* zone = editorState.findZone(*zoneId);
      return "Zone selected: " + (zone != nullptr ? zone->effect : *zoneId);
    }
    if (const auto& triggerId = editorState.selectedTriggerId(); triggerId.has_value()) {
      // Mirrors TriggersPanel.cpp's own "Checkpoint"/"Dummy" + " (Finish)" labeling convention.
      if (const editor::Trigger* trigger = editorState.findTrigger(*triggerId)) {
        const bool isCheckpoint = trigger->type == "checkpoint";
        return std::string("Trigger selected: ") + (isCheckpoint ? "Checkpoint" : "Dummy") +
               (isCheckpoint && trigger->role == "finish" ? " (Finish)" : "");
      }
      return "Trigger selected: " + *triggerId;
    }
    return "Nothing selected";
  };

  auto rebake = [&]() {
    // Skip the expensive self-intersection detection pass while a point/mesh drag is in progress
    // (this lambda gets called every frame of an active drag, since dragging mutates the track
    // every frame). Reuse the last good detection result instead, so markers stay visible (just
    // briefly stale) mid-drag rather than flickering empty.
    const bool detect = !editorState.dragging();
    bakedResult = tox::Track::fromJson(editor::toJson(editorState.track()), detect);
    if (bakedResult.track.has_value()) {
      if (detect)
        cachedCrossings = bakedResult.track->selfIntersections;
      else
        bakedResult.track->selfIntersections = cachedCrossings;
    }
    bakedTrack = bakedResult ? &*bakedResult.track : nullptr;
  };

  // Document state is deliberately separate from TrackDefinition: JSON's editable metadata name
  // may change, while a loaded/first-saved Resource@name remains the stable save identity.
  std::optional<editor::TrackSaveBinding> saveBinding;
  std::optional<std::string> cleanTrackJson = editor::toJson(editorState.track());
  auto documentDirty = [&]() {
    return !cleanTrackJson.has_value() || editor::toJson(editorState.track()) != *cleanTrackJson;
  };

  auto materialMap = [&]() {
    std::map<std::string, std::string> result;
    for (const editor::MaterialEntry& entry : materialCatalog.materials())
      result.emplace(entry.qualifiedName, entry.materialQualifiedName);
    return result;
  };
  auto unresolvedMaterial = [&]() -> std::optional<std::string> {
    std::set<std::string> known;
    for (const editor::MaterialEntry& entry : materialCatalog.materials()) known.insert(entry.qualifiedName);
    for (const editor::Path& path : editorState.track().paths)
      if (path.material.empty() || !known.count(path.material)) return path.material.empty() ? "(unassigned)" : path.material;
    return std::nullopt;
  };
  auto installMaterialNames = [&]() {
    std::vector<std::string> names;
    names.reserve(materialCatalog.materials().size());
    for (const auto& entry : materialCatalog.materials()) names.push_back(entry.qualifiedName);
    editorState.setAvailableMaterials(std::move(names));
  };
  auto refreshMaterials = [&]() {
    try {
      editor::MaterialCatalog refreshed = editor::MaterialCatalog::load(materialResourcesPath, textureCache);
      materialCatalog = std::move(refreshed);
      installMaterialNames();
      rebake();
      showStatus("Refreshed " + std::to_string(materialCatalog.materials().size()) + " material(s) from " +
                 editor::pathToUtf8(materialResourcesPath));
    } catch (const std::exception& error) {
      showStatus(std::string("Material refresh failed; previous catalog retained: ") + error.what());
    }
  };

  auto replaceDocument = [&](editor::TrackDefinition track,
                             std::optional<editor::TrackSaveBinding> binding,
                             bool dirty) {
    editorState.history().clear();
    editorState.replaceTrack(std::move(track));
    saveBinding = std::move(binding);
    cleanTrackJson = dirty ? std::nullopt : std::optional<std::string>(editor::toJson(editorState.track()));
    topDownView.resetView();
    rebake();
  };

  enum class DocumentAction { None,
                              New,
                              OpenResources,
                              ImportJson,
                              Exit };
  DocumentAction requestedAction = DocumentAction::None;
  DocumentAction pendingDirtyAction = DocumentAction::None;
  DocumentAction readyAction = DocumentAction::None;

  std::filesystem::path chooserXmlPath;
  editor::TrackResourceScanResult chooserScan;
  int chooserSelectedTrack = -1;
  bool openTrackChooser = false;
  std::optional<editor::TrackSavePlan> pendingSavePlan;
  bool openOverwriteConfirmation = false;
  bool openSaveConflict = false;
  std::string saveConflictMessage;

  auto loadTrackCandidate = [&](const editor::TrackResourceCandidate& candidate) {
    if (!candidate.loadable()) return;
    std::string modelRef = candidate.modelFileReference;
    if (!editor::isSafeResourceRelativePath(modelRef))
      modelRef = editor::sanitizeTrackResourceFilenameStem(candidate.resourceName) + ".mppmodel";
    editor::TrackSaveBinding binding{chooserXmlPath, candidate.resourceName,
                                     candidate.trackDataReference, modelRef,
                                     candidate.resourceFingerprint, candidate.jsonFingerprint};
    replaceDocument(*candidate.track, std::move(binding), false);
    showStatus("Loaded Tracks/" + candidate.resourceName +
               (candidate.warning.empty() ? "" : " -- " + candidate.warning));
  };

  auto finishSuccessfulSave = [&](const editor::TrackSavePlan& plan) {
    saveBinding = plan.resultingBinding;
    cleanTrackJson = editor::toJson(editorState.track());
    showStatus("Saved Tracks/" + saveBinding->resourceName + " to " + editor::pathToUtf8(saveBinding->xmlPath));
    if (pendingDirtyAction != DocumentAction::None) {
      readyAction = pendingDirtyAction;
      pendingDirtyAction = DocumentAction::None;
    }
  };
  auto commitPreparedSave = [&](const editor::TrackSavePlan& plan) {
    try {
      std::string error;
      if (!editor::commitTrackSave(plan, error)) {
        showStatus("Save failed; XML, JSON and model were left unchanged: " + error);
        return false;
      }
      finishSuccessfulSave(plan);
      return true;
    } catch (const std::exception& error) {
      showStatus(std::string("Save failed; XML, JSON and model were left unchanged: ") + error.what());
      return false;
    }
  };
  auto beginSave = [&](bool forceSaveAs) {
    if (bakedTrack == nullptr) {
      showStatus("Cannot save -- current track failed to bake");
      return false;
    }
    if (const auto unresolved = unresolvedMaterial()) {
      showStatus("Cannot save -- unavailable TrackMaterial: " + *unresolved);
      return false;
    }

    const bool saveAs = forceSaveAs || !saveBinding.has_value();
    std::filesystem::path xmlPath;
    if (saveAs) {
      const std::string identity = saveBinding ? saveBinding->resourceName : (editorState.track().name.empty() ? "Track" : editorState.track().name);
      const editor::FileDialogResult picked = editor::showSaveFileDialog(
          L"Save Track to Resources XML", {{L"Resources XML (*.xml)", L"*.xml"}},
          toWide(editor::sanitizeTrackResourceFilenameStem(identity) + ".xml"), L"xml", false);
      if (!picked.ok) return false;
      xmlPath = picked.path;
    } else {
      xmlPath = saveBinding->xmlPath;
    }

    editor::TrackSavePlan plan;
    try {
      plan = editor::prepareTrackSave(editorState.track(), *bakedTrack, materialMap(), xmlPath,
                                      saveBinding, saveAs);
    } catch (const std::exception& error) {
      showStatus(std::string("Save preparation failed: ") + error.what());
      return false;
    }
    if (!plan.ok()) {
      if (plan.errorKind == editor::TrackSaveErrorKind::ExternalXmlConflict ||
          plan.errorKind == editor::TrackSaveErrorKind::ExternalJsonConflict) {
        saveConflictMessage = plan.error;
        openSaveConflict = true;
      } else {
        showStatus("Save failed: " + plan.error);
      }
      return false;
    }
    if (plan.requiresConfirmation()) {
      pendingSavePlan = std::move(plan);
      openOverwriteConfirmation = true;
      return false;
    }
    return commitPreparedSave(plan);
  };

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) requestedAction = DocumentAction::Exit;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window)) {
        requestedAction = DocumentAction::Exit;
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // E/C switch mode, Ctrl+Z/Ctrl+Y undo/redo -- all global
    // since there's no text-input widget yet that would need to steal these keys.
    if (!io.WantTextInput) {
      if (ImGui::IsKeyPressed(ImGuiKey_E)) editorState.setMode(editor::EditMode::Edit);
      if (ImGui::IsKeyPressed(ImGuiKey_C)) editorState.setMode(editor::EditMode::Create);
      // 1/2/3 switch canvas projection mode -- orthogonal to E/C's EditMode, so both live on
      // the global (non-text-input) shortcut set without colliding.
      if (ImGui::IsKeyPressed(ImGuiKey_1)) editorState.setProjectionMode(editor::ProjectionMode::TopDown);
      if (ImGui::IsKeyPressed(ImGuiKey_2)) editorState.setProjectionMode(editor::ProjectionMode::Front);
      if (ImGui::IsKeyPressed(ImGuiKey_3)) editorState.setProjectionMode(editor::ProjectionMode::Side);
      if (ImGui::IsKeyPressed(ImGuiKey_G)) topDownView.setShowGrid(!topDownView.showGrid());
      // Deselect: clears whichever of point/mesh-region/
      // zone/trigger is currently selected -- mirrored by the Edit menu's "Deselect" item below.
      if (ImGui::IsKeyPressed(ImGuiKey_D)) editorState.deselectAll();
      const bool ctrl = io.KeyCtrl;
      // Home / zoom-to-selection: plain 'z'/'x', not
      // Ctrl-modified, so Ctrl+Z for Undo just below still works. Mirrored by the View menu
      // entries and (for zoom-to-selection) the top-down canvas's own "Object" button -- all three
      // go through the same TopDownView::resetView()/editor::FocusOnSelection() calls.
      if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) topDownView.resetView();
      if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_X)) editor::FocusOnSelection(topDownView, editorState, bakedTrack);
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z) && editorState.undo()) rebake();
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y) && editorState.redo()) rebake();
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O)) requestedAction = DocumentAction::OpenResources;
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) beginSave(io.KeyShift);
    }

    // --- Fixed layout: menu bar, toolbar, dockspace (left panel / top-down filling the rest,
    // EDITOR_CPP_PORT_PLAN.md-adjacent UI pass) -------------------------------
    //
    // File/Random/View menu actions below reuse exactly the same EditorState/TopDownView calls
    // the old single "track_editor — status" mega-window made inline; only their container moved
    // (menu item / toolbar button / Diagnostics panel), not their logic.
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New")) requestedAction = DocumentAction::New;
        if (ImGui::MenuItem("Open Resources XML...", "Ctrl+O")) requestedAction = DocumentAction::OpenResources;
        if (ImGui::MenuItem("Save", "Ctrl+S")) beginSave(false);
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) beginSave(true);
        if (ImGui::MenuItem("Refresh materials from XML")) refreshMaterials();
        ImGui::Separator();
        if (ImGui::MenuItem("Import Track JSON...")) requestedAction = DocumentAction::ImportJson;
        if (ImGui::MenuItem("Export JSON...")) {
          const editor::FileDialogResult picked = editor::showSaveFileDialog(
              L"Export Track JSON", {{L"Track JSON (*.json)", L"*.json"}}, toWide(sanitizeFilenameStem(editorState.track().name) + ".json"), L"json");
          if (picked.ok) {
            // toFile() throws when the stream won't open (read-only target, locked file, removed
            // drive) -- uncaught here it took the whole process down and the user's unsaved track
            // with it. Import JSON already caught; this didn't.
            try {
              editor::toFile(editorState.track(), picked.path);
              showStatus("Wrote " + editor::pathToUtf8(picked.path));
            } catch (const std::exception& error) {
              showStatus(std::string("Export failed: ") + error.what());
            }
          }
        }
        ImGui::Separator();
        // "Load Model..." picks either a raw .mppmodel or a standalone <Model> XML fragment (the
        // latter parsed via cpp/model-xml, same as model-tool's own OpenTarget.cpp classification --
        // reimplemented independently here, not shared, since the editor has no dependency on
        // model-tool's code) and embeds it into the current Track's `models` list (or reuses an
        // existing entry, per the dedup-by-ModelFile decision) -- it does NOT place an instance.
        // Placing instances is the canvas's right-click "Place Model" submenu's job instead (see
        // TopDownCanvas.cpp), so the same embedded Model can be placed any number of times without
        // reopening its source file each time.
        if (ImGui::MenuItem("Load Model...")) {
          const editor::FileDialogResult picked = editor::showOpenFileDialog(
              L"Load Model", {{L"MassivePolyPusher Model (*.mppmodel)", L"*.mppmodel"}, {L"Model XML (*.xml)", L"*.xml"}});
          if (picked.ok) {
            auto resolveModelFileReference = [&](const std::filesystem::path& absoluteMppModelPath) {
              if (saveBinding.has_value()) {
                std::error_code relError;
                const std::filesystem::path relative =
                    std::filesystem::relative(absoluteMppModelPath, saveBinding->xmlPath.parent_path(), relError);
                if (!relError && !relative.empty()) return editor::pathToUtf8(relative);
              }
              // No save location bound yet -- store the absolute path so placement rendering can
              // still resolve it this session (TopDownCanvas.cpp's loadCachedPlacementGeometry
              // joins ModelFile against the track's save directory, but std::filesystem::path's
              // `/` operator returns the right-hand side unchanged when it's already absolute, so
              // this round-trips correctly even before modelBaseDir exists). The author can still
              // retype a real relative path once the track has a home (Properties panel) -- a bare
              // filename here would have silently discarded the only path that worked, whereas an
              // absolute path is only inconvenient, never wrong.
              return editor::pathToUtf8(absoluteMppModelPath);
            };

            const bool isModelXml = picked.path.extension() == L".xml" || picked.path.extension() == L".XML";
            try {
              modelxml::ModelXmlDefinition parsed;
              std::string modelFileReference;
              if (isModelXml) {
                parsed = modelxml::loadStandaloneModelXml(picked.path);
                const std::filesystem::path absoluteMppModel =
                    (picked.path.parent_path() / editor::utf8ToWide(parsed.modelFile)).lexically_normal();
                modelFileReference = resolveModelFileReference(absoluteMppModel);
              } else {
                // A raw .mppmodel with no associated XML has no Type/Visible metadata to attach --
                // EditorState::embedModel embeds a bare entry (matching model-tool's own "no XML
                // metadata yet" Physical/visible defaults).
                modelFileReference = resolveModelFileReference(picked.path);
              }
              editorState.embedModel(std::move(parsed), modelFileReference);
              showStatus("Loaded " + modelFileReference + " -- right-click the canvas to place an instance");
            } catch (const std::exception& error) {
              showStatus(std::string("Load Model failed: ") + error.what());
            }
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
                showStatus("Wrote " + editor::pathToUtf8(picked.path) + " (" + std::to_string(usd.meshCount) + " mesh(es))");
              } else {
                showStatus("Failed to open " + editor::pathToUtf8(picked.path) + " for writing");
              }
            }
          } else {
            showStatus("Nothing to export -- current track failed to bake");
          }
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Edit")) {
        // Undo/redo disabled state: disabled while their respective stack is empty rather than
        // always active.
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, editorState.history().canUndo())) {
          if (editorState.undo()) rebake();
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, editorState.history().canRedo())) {
          if (editorState.redo()) rebake();
        }
        ImGui::Separator();
        // Deselect: mirrors the 'D' hotkey above.
        if (ImGui::MenuItem("Deselect", "D")) editorState.deselectAll();
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("View")) {
        // Home / zoom-to-selection: mirror the 'z'/'x'
        // hotkeys above; zoom-to-selection is also on the top-down canvas's own "Object" button.
        if (ImGui::MenuItem("Home", "Z")) topDownView.resetView();
        if (ImGui::MenuItem("Zoom to Selection", "X")) editor::FocusOnSelection(topDownView, editorState, bakedTrack);
        ImGui::Separator();
        // Top-down grid display / grid size / snap-to-grid. Hiding the grid
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
        // Render mode. kRenderModes is also what the toolbar's render-mode combobox iterates, so
        // the two pickers can't drift out of sync with each other.
        if (ImGui::BeginMenu("Render Mode")) {
          for (const auto& [label, mode] : kRenderModes) {
            if (ImGui::MenuItem(label, nullptr, topDownView.renderMode() == mode)) topDownView.setRenderMode(mode);
          }
          ImGui::EndMenu();
        }
        ImGui::Separator();
        // Point-type filters. Only Position currently has an observable effect -- roll/width/
        // crossSection points have no on-canvas presence yet at all, so those three
        // checkboxes exist ready for that but are otherwise inert until that on-canvas rendering
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
        // Physics-sample overlay.
        bool showPhysicsPoints = topDownView.showPhysicsPoints();
        if (ImGui::MenuItem("Show Physics Points", nullptr, &showPhysicsPoints)) topDownView.setShowPhysicsPoints(showPhysicsPoints);
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    if (requestedAction != DocumentAction::None) {
      if (documentDirty()) {
        pendingDirtyAction = requestedAction;
        ImGui::OpenPopup("Unsaved Changes");
      } else {
        readyAction = requestedAction;
      }
      requestedAction = DocumentAction::None;
    }

    if (openOverwriteConfirmation) {
      ImGui::OpenPopup("Confirm Track Overwrite");
      openOverwriteConfirmation = false;
    }
    if (openSaveConflict) {
      ImGui::OpenPopup("Save Conflict");
      openSaveConflict = false;
    }
    if (openTrackChooser) {
      ImGui::OpenPopup("Load Track from Resources XML");
      openTrackChooser = false;
    }

    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("The current track has unsaved changes.");
      if (ImGui::Button("Save")) {
        const bool saved = beginSave(false);
        if (saved || pendingSavePlan.has_value() || openSaveConflict) ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard")) {
        readyAction = pendingDirtyAction;
        pendingDirtyAction = DocumentAction::None;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        pendingDirtyAction = DocumentAction::None;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Confirm Track Overwrite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("Saving will replace:");
      if (pendingSavePlan)
        for (const std::string& warning : pendingSavePlan->overwriteWarnings)
          ImGui::BulletText("%s", warning.c_str());
      if (ImGui::Button("Overwrite") && pendingSavePlan) {
        editor::TrackSavePlan plan = std::move(*pendingSavePlan);
        pendingSavePlan.reset();
        if (!commitPreparedSave(plan)) pendingDirtyAction = DocumentAction::None;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        pendingSavePlan.reset();
        pendingDirtyAction = DocumentAction::None;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Save Conflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", saveConflictMessage.c_str());
      if (ImGui::Button("Reload") && saveBinding) {
        const editor::TrackResourceScanResult scan = editor::scanTrackResources(saveBinding->xmlPath);
        const auto found = std::find_if(scan.tracks.begin(), scan.tracks.end(),
                                        [&](const auto& candidate) {
                                          return candidate.resourceName == saveBinding->resourceName && candidate.loadable();
                                        });
        if (scan.validDocument() && found != scan.tracks.end()) {
          std::string modelRef = found->modelFileReference;
          if (!editor::isSafeResourceRelativePath(modelRef))
            modelRef = editor::sanitizeTrackResourceFilenameStem(found->resourceName) + ".mppmodel";
          editor::TrackSaveBinding refreshed{saveBinding->xmlPath, found->resourceName,
                                             found->trackDataReference, modelRef,
                                             found->resourceFingerprint, found->jsonFingerprint};
          replaceDocument(*found->track, std::move(refreshed), false);
          showStatus("Reloaded Tracks/" + found->resourceName);
        } else {
          showStatus("Reload failed: bound Track resource is no longer loadable");
        }
        pendingDirtyAction = DocumentAction::None;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Save As...")) {
        if (!beginSave(true) && !pendingSavePlan.has_value()) pendingDirtyAction = DocumentAction::None;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        pendingDirtyAction = DocumentAction::None;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(760.0f, 480.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Load Track from Resources XML")) {
      ImGui::TextUnformatted("Choose a track resource to open.");
      ImGui::TextDisabled("Resources XML");
      ImGui::SameLine();
      ImGui::TextWrapped("%s", editor::pathToUtf8(chooserXmlPath).c_str());
      ImGui::Separator();
      ImGui::TextDisabled("%zu track resource%s", chooserScan.tracks.size(),
                          chooserScan.tracks.size() == 1 ? "" : "s");

      bool acceptSelected = false;
      constexpr ImGuiTableFlags chooserTableFlags =
          ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH |
          ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
          ImGuiTableFlags_SizingStretchProp;
      if (ImGui::BeginTable("trackResources", 3, chooserTableFlags, ImVec2(0.0f, 270.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn("Track data", ImGuiTableColumnFlags_WidthStretch, 0.46f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < chooserScan.tracks.size(); ++i) {
          const editor::TrackResourceCandidate& candidate = chooserScan.tracks[i];
          ImGui::PushID(static_cast<int>(i));
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          if (ImGui::Selectable(candidate.resourceName.c_str(),
                                chooserSelectedTrack == static_cast<int>(i),
                                ImGuiSelectableFlags_SpanAllColumns |
                                    ImGuiSelectableFlags_AllowDoubleClick)) {
            chooserSelectedTrack = static_cast<int>(i);
            if (candidate.loadable() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
              acceptSelected = true;
          }
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tracks/%s", candidate.resourceName.c_str());

          ImGui::TableSetColumnIndex(1);
          const char* trackData = candidate.trackDataReference.empty()
                                      ? "(not available)"
                                      : candidate.trackDataReference.c_str();
          ImGui::TextUnformatted(trackData);
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", trackData);

          ImGui::TableSetColumnIndex(2);
          if (!candidate.error.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Invalid");
          else if (!candidate.warning.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "Warning");
          else
            ImGui::TextDisabled("Ready");
          ImGui::PopID();
        }
        ImGui::EndTable();
      }

      const editor::TrackResourceCandidate* selected =
          chooserSelectedTrack >= 0 &&
                  chooserSelectedTrack < static_cast<int>(chooserScan.tracks.size())
              ? &chooserScan.tracks[static_cast<std::size_t>(chooserSelectedTrack)]
              : nullptr;
      if (chooserScan.tracks.empty()) {
        ImGui::TextDisabled("No Track resources were found in this document.");
      } else if (selected == nullptr) {
        ImGui::TextDisabled("Select a track to see its details.");
      } else if (!selected->error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("Cannot load: %s", selected->error.c_str());
        ImGui::PopStyleColor();
      } else if (!selected->warning.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.72f, 0.25f, 1.0f));
        ImGui::TextWrapped("Warning: %s", selected->warning.c_str());
        ImGui::PopStyleColor();
      } else {
        ImGui::TextDisabled("Ready to load Tracks/%s", selected->resourceName.c_str());
      }

      ImGui::Separator();
      const float buttonWidth = 100.0f;
      const float buttonsWidth = buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
      const float rightAlignedX =
          ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonsWidth;
      if (rightAlignedX > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(rightAlignedX);
      ImGui::BeginDisabled(selected == nullptr || !selected->loadable());
      if (ImGui::Button("Load Track", ImVec2(buttonWidth, 0.0f))) acceptSelected = true;
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f))) ImGui::CloseCurrentPopup();

      if (acceptSelected && selected != nullptr && selected->loadable()) {
        loadTrackCandidate(*selected);
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (readyAction != DocumentAction::None) {
      const DocumentAction action = readyAction;
      readyAction = DocumentAction::None;
      if (action == DocumentAction::New) {
        replaceDocument(buildStarterTrack(), std::nullopt, false);
        showStatus("Created a new unbound track");
      } else if (action == DocumentAction::OpenResources) {
        const editor::FileDialogResult picked = editor::showOpenFileDialog(
            L"Open Resources XML", {{L"Resources XML (*.xml)", L"*.xml"}});
        if (picked.ok) {
          chooserXmlPath = picked.path;
          chooserScan = editor::scanTrackResources(picked.path);
          chooserSelectedTrack = -1;
          if (!chooserScan.validDocument()) {
            showStatus("Open failed: " + chooserScan.error);
          } else {
            const auto firstLoadable =
                std::find_if(chooserScan.tracks.begin(), chooserScan.tracks.end(),
                             [](const editor::TrackResourceCandidate& candidate) {
                               return candidate.loadable();
                             });
            if (firstLoadable != chooserScan.tracks.end())
              chooserSelectedTrack =
                  static_cast<int>(std::distance(chooserScan.tracks.begin(), firstLoadable));
            openTrackChooser = true;
          }
        }
      } else if (action == DocumentAction::ImportJson) {
        const editor::FileDialogResult picked = editor::showOpenFileDialog(
            L"Import Track JSON", {{L"Track JSON (*.json)", L"*.json"}});
        if (picked.ok) {
          try {
            replaceDocument(editor::fromFile(picked.path), std::nullopt, true);
            showStatus("Imported " + editor::pathToUtf8(picked.path));
          } catch (const std::exception& error) {
            showStatus(std::string("Import failed: ") + error.what());
          }
        }
      } else if (action == DocumentAction::Exit) {
        running = false;
      }
    }

    // Toolbar: a fixed strip pinned directly under the menu bar (not part of the dockspace, not
    // movable/resizable) for the handful of controls used constantly regardless of which panel
    // tab is focused -- document actions, mode and quick undo/redo. Everything else that used to live in the old
    // single "track_editor — status" mega-window moved into the menu bar above, the Panels window's
    // sections (Track name/direction now in "Track Properties", first in the list), or the
    // Diagnostics panel.
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    // mainViewport->WorkPos.y already excludes the main menu bar -- BeginMainMenuBar()/
    // EndMainMenuBar() shrink the platform viewport's work area for it automatically, so adding a
    // second menu-bar-height offset here would leave a visible gap between the two strips.
    ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x, mainViewport->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::Begin("##Toolbar", nullptr,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoSavedSettings);
    const ImVec2 documentButtonSize(ImGui::GetFrameHeight(), 0.0f);
    if (ImGui::Button(ICON_FA_FILE "##NewTrackToolbar", documentButtonSize))
      requestedAction = DocumentAction::New;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New track");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "##OpenTrackToolbar", documentButtonSize))
      requestedAction = DocumentAction::OpenResources;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open Resources XML (Ctrl+O)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SAVE "##SaveTrackToolbar", documentButtonSize)) beginSave(false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save track (Ctrl+S)");
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextUnformatted("Mode");
    ImGui::SameLine();
    int modeIndex = static_cast<int>(editorState.mode());
    const char* modeNames[] = {"Edit", "Create"};
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("##mode", &modeIndex, modeNames, 2)) editorState.setMode(static_cast<editor::EditMode>(modeIndex));
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextUnformatted("View");
    ImGui::SameLine();
    {
      int projectionIndex = static_cast<int>(editorState.projectionMode());
      const char* projectionNames[] = {"Top-down (1)", "Front (2)", "Side (3)"};
      ImGui::SetNextItemWidth(130);
      if (ImGui::Combo("##projectionMode", &projectionIndex, projectionNames, 3))
        editorState.setProjectionMode(static_cast<editor::ProjectionMode>(projectionIndex));
    }
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextUnformatted("Render");
    ImGui::SameLine();
    {
      int renderModeIndex = 0;
      for (int i = 0; i < static_cast<int>(std::size(kRenderModes)); ++i)
        if (kRenderModes[i].second == topDownView.renderMode()) renderModeIndex = i;
      ImGui::SetNextItemWidth(190);
      if (ImGui::BeginCombo("##renderMode", kRenderModes[renderModeIndex].first)) {
        for (const auto& [label, mode] : kRenderModes) {
          const bool selected = mode == topDownView.renderMode();
          if (ImGui::Selectable(label, selected)) topDownView.setRenderMode(mode);
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!editorState.history().canUndo());
    if (ImGui::Button(ICON_FA_UNDO " Undo")) {
      if (editorState.undo()) rebake();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!editorState.history().canRedo());
    if (ImGui::Button(ICON_FA_REDO " Redo")) {
      if (editorState.redo()) rebake();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    // Point-type filters, same toggles as the View > Point Filters
    // submenu below -- both read/write the same TopDownView state, so either one moves the other.
    // Only Position currently has an observable effect -- roll/width/crossSection points have no
    // on-canvas presence yet at all, so those three checkboxes exist ready for that but are
    // otherwise inert until that on-canvas rendering lands.
    ImGui::TextUnformatted("Show:");
    ImGui::SameLine();
    bool showPositionToolbar = topDownView.showPositionPoints();
    if (ImGui::Checkbox("Position##toolbar", &showPositionToolbar)) topDownView.setShowPositionPoints(showPositionToolbar);
    ImGui::SameLine();
    bool showRollToolbar = topDownView.showRollPoints();
    if (ImGui::Checkbox("Roll##toolbar", &showRollToolbar)) topDownView.setShowRollPoints(showRollToolbar);
    ImGui::SameLine();
    bool showWidthToolbar = topDownView.showWidthPoints();
    if (ImGui::Checkbox("Width##toolbar", &showWidthToolbar)) topDownView.setShowWidthPoints(showWidthToolbar);
    ImGui::SameLine();
    bool showCrossSectionToolbar = topDownView.showCrossSectionPoints();
    if (ImGui::Checkbox("Cross-Section##toolbar", &showCrossSectionToolbar)) topDownView.setShowCrossSectionPoints(showCrossSectionToolbar);
    const float toolbarHeight = ImGui::GetWindowSize().y;
    ImGui::End();
    ImGui::PopStyleVar();

    // Status bar: a fixed strip docked to the bottom of the window, showing the most recent
    // showStatus() message for 3 seconds. Its height is fixed
    // up front (unlike the toolbar's auto-measured height above) so the dockspace host below can
    // subtract it out in the same pass it's positioned in, rather than needing a second frame.
    const float statusBarHeight = ImGui::GetTextLineHeightWithSpacing() + 16.0f;
    const bool statusVisible = !statusMessage.empty() && std::chrono::steady_clock::now() < statusExpiresAt;

    // Dockspace host: fills the remaining viewport between the toolbar and the status bar. The
    // layout itself (left panel with every property/tool panel tabbed together, top-down view
    // filling the rest) is built once via DockBuilder on the first frame only, then never touched
    // again -- io.IniFilename is null (see CreateContext above), so there's no saved layout to
    // conflict with, and every future launch starts from this exact same arrangement. Used to also
    // split the right side top/bottom for the Elevation Profile panel, retired in
    // DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 1.4 -- Top-Down View now takes the whole right side.
    ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x, mainViewport->WorkPos.y + toolbarHeight));
    ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, mainViewport->WorkSize.y - toolbarHeight - statusBarHeight));
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

      ImGui::DockBuilderDockWindow("Panels", leftId);
      ImGui::DockBuilderDockWindow("View", rightId);

      ImGui::DockBuilderFinish(dockspaceId);
    }
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();

    // Mirrors currentCurve(): see EditorState::currentPathIndex()'s own comment for how "current"
    // is resolved (selection wins while a point is selected; otherwise the curve-selector dropdown).
    // Computed up here (rather than between Top-Down View and this window, as before) since every
    // section below that needs it now lives in the same "Panels" window.
    const int currentPathIndex = editorState.track().paths.empty() ? -1 : editorState.currentPathIndex();

    // Left-docked panel: every property/tool section as
    // a collapsing header in one window, rather than separate tabbed windows -- CollapsingHeader
    // does NOT push an ID scope onto what follows it (unlike TreeNode), so each section's content
    // is wrapped in its own PushID/PopID to keep same-labelled widgets in different sections
    // (e.g. both HandlingPanel and RandomRangesPanel have a "Reset to Default" button) from
    // colliding on ImGui ID.
    ImGui::Begin("Panels");
    if (ImGui::CollapsingHeader("Track Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::PushID("TrackProperties");
      if (editor::DrawTrackPropertiesPanel(editorState)) rebake();
      ImGui::PopID();
    }
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
      if (editor::DrawTriggersPanel(editorState, currentPathIndex, bakedTrack)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Reservations")) {
      ImGui::PushID("Reservations");
      if (editor::DrawReservationsPanel(editorState, currentPathIndex, bakedTrack)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Curves")) {
      ImGui::PushID("Curves");
      if (editor::DrawCurvesPanel(editorState)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Models")) {
      ImGui::PushID("Models");
      editor::DrawModelsPanel(editorState);
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Model Placements")) {
      ImGui::PushID("ModelPlacements");
      if (editor::DrawModelPlacementsPanel(editorState)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Materials")) {
      ImGui::PushID("Materials");
      if (ImGui::Button("Refresh materials from XML")) refreshMaterials();
      ImGui::TextDisabled("%s", editor::pathToUtf8(materialResourcesPath).c_str());
      if (editor::DrawMaterialsPanel(editorState, materialCatalog, textureCache, currentPathIndex)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Handling")) {
      ImGui::PushID("Handling");
      if (editor::DrawHandlingPanel(editorState)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Random Generation")) {
      ImGui::PushID("RandomGeneration");
      if (editor::DrawRandomRangesPanel(editorState, randomRanges, randomSeed, randomComplexity)) rebake();
      ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Diagnostics")) {
      ImGui::PushID("Diagnostics");
      ImGui::TextUnformatted("SDL3 + OpenGL3 + ImGui (docking) + gl3w link up.");
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
      ImGui::TextUnformatted("M6 smoke check (elevation drag, exercised directly):");
      ImGui::BulletText("elevation drag / undo / redo: %s / %s / %s", m6Smoke.elevationChanged ? "OK" : "MISMATCH",
                        m6Smoke.undone ? "OK" : "MISMATCH", m6Smoke.redone ? "OK" : "MISMATCH");
      ImGui::TextUnformatted("M7a smoke check (random-track bake + USD export, exercised directly):");
      ImGui::BulletText("random track bakes: %s (%zu path(s), %zu geometry batch(es))", m7aSmoke.randomBakeOk ? "OK" : "FAILED",
                        m7aSmoke.randomPathCount, m7aSmoke.randomGeometryBatchCount);
      ImGui::BulletText("USD export header / meshes: %s / %s (%zu mesh(es))", m7aSmoke.usdHeaderOk ? "OK" : "MISMATCH",
                        m7aSmoke.usdHasMeshes ? "OK" : "MISMATCH", m7aSmoke.usdMeshCount);
      ImGui::TextUnformatted("MppModel smoke check (MPPMODEL_EXPORT_SPEC.md, exercised directly):");
      ImGui::BulletText("header / mesh count / fields match / byte sizes match / non-indexed: %s / %s / %s / %s / %s",
                        mppModelSmoke.headerOk ? "OK" : "MISMATCH", mppModelSmoke.meshCountMatches ? "OK" : "MISMATCH",
                        mppModelSmoke.fieldsMatch ? "OK" : "MISMATCH", mppModelSmoke.byteSizesMatch ? "OK" : "MISMATCH",
                        mppModelSmoke.nonIndexedTriangleSoup ? "OK" : "MISMATCH");
      ImGui::TextUnformatted("M7b smoke check (texture assets, exercised directly):");
      ImGui::BulletText("image size read / add asset / assign: %s / %s / %s", m7bSmoke.imageSizeReadOk ? "OK" : "MISMATCH",
                        m7bSmoke.assetAdded ? "OK" : "MISMATCH", m7bSmoke.assigned ? "OK" : "MISMATCH");
      ImGui::BulletText("tile resize keeps valid binding / clears invalid one / delete: %s / %s / %s",
                        m7bSmoke.tileResizeOk ? "OK" : "MISMATCH", m7bSmoke.invalidAssignmentCleared ? "OK" : "MISMATCH",
                        m7bSmoke.deleted ? "OK" : "MISMATCH");
      ImGui::TextUnformatted("Parity-fix smoke check (exercised directly):");
      ImGui::BulletText("no id collision on Create / drawn path bakes as drawn: %s / %s",
                        paritySmoke.noIdCollisionOnCreate ? "OK" : "MISMATCH", paritySmoke.drawnPathBakesAsDrawn ? "OK" : "MISMATCH");
      ImGui::BulletText("start point preserved / clamped in range on delete: %s / %s",
                        paritySmoke.startPointPreservedOnDelete ? "OK" : "MISMATCH", paritySmoke.startClampedInRange ? "OK" : "MISMATCH");
      ImGui::TextUnformatted("Gap1 smoke check (roll/width/crossSection point editing, exercised directly):");
      ImGui::BulletText("add roll/width/crossSection / edit fields / delete: %s/%s/%s / %s / %s", gap1Smoke.rollAdded ? "OK" : "MISMATCH",
                        gap1Smoke.widthAdded ? "OK" : "MISMATCH", gap1Smoke.crossSectionAdded ? "OK" : "MISMATCH",
                        gap1Smoke.fieldsEdited ? "OK" : "MISMATCH", gap1Smoke.deleted ? "OK" : "MISMATCH");
      ImGui::BulletText("baked roll/width reflect the edit: %s / %s", gap1Smoke.bakedRollApplied ? "OK" : "MISMATCH",
                        gap1Smoke.bakedWidthApplied ? "OK" : "MISMATCH");
      ImGui::BulletText("4-position floor holds / aux points unguarded by it: %s / %s",
                        gap1Smoke.deletingBelowFourPositionsRefused ? "OK" : "MISMATCH", gap1Smoke.deletingAuxPointsUnguarded ? "OK" : "MISMATCH");
      ImGui::BulletText("selectionIsPosition true for position / false for aux / false when invalid: %s / %s / %s",
                        gap1Smoke.selectionIsPositionTrueForPosition ? "OK" : "MISMATCH", gap1Smoke.selectionIsPositionFalseForAux ? "OK" : "MISMATCH",
                        gap1Smoke.selectionIsPositionFalseWhenInvalid ? "OK" : "MISMATCH");
      ImGui::BulletText("width-drag: selectionIsWidth true/false / dragged / clamps to floor: %s / %s / %s / %s",
                        gap1Smoke.selectionIsWidthTrueForWidth ? "OK" : "MISMATCH", gap1Smoke.selectionIsWidthFalseForPosition ? "OK" : "MISMATCH",
                        gap1Smoke.widthDragged ? "OK" : "MISMATCH", gap1Smoke.widthDragClampsToFloor ? "OK" : "MISMATCH");
      ImGui::BulletText("width-drag: undo restores / refused while a position point is selected: %s / %s",
                        gap1Smoke.widthDragUndone ? "OK" : "MISMATCH", gap1Smoke.widthDragRefusedForPositionSelection ? "OK" : "MISMATCH");
      ImGui::BulletText("roll-drag: selectionIsRoll true/false / dragged / clamps to range: %s / %s / %s / %s",
                        gap1Smoke.selectionIsRollTrueForRoll ? "OK" : "MISMATCH", gap1Smoke.selectionIsRollFalseForPosition ? "OK" : "MISMATCH",
                        gap1Smoke.rollDragged ? "OK" : "MISMATCH", gap1Smoke.rollDragClampsToRange ? "OK" : "MISMATCH");
      ImGui::BulletText("roll-drag: undo restores / refused while a position point is selected: %s / %s",
                        gap1Smoke.rollDragUndone ? "OK" : "MISMATCH", gap1Smoke.rollDragRefusedForPositionSelection ? "OK" : "MISMATCH");
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
    ImGui::Begin("View");
    std::optional<editor::WorldPoint2D> hoveredWorld;
    // Placement geometry (TRACK_MODEL_LIST_PLAN.md Milestone 4.2): an embedded Model's own
    // <ModelFile> reference is resolved relative to the current save location's directory.
    const std::filesystem::path modelBaseDir = saveBinding.has_value() ? saveBinding->xmlPath.parent_path() : std::filesystem::path{};
    if (editor::DrawTopDownCanvas(topDownView, editorState, bakedTrack, &hoveredWorld, modelBaseDir)) rebake();
    ImGui::End();

    // Status bar strip itself: docked to the bottom edge, same fixed/non-dockable/non-movable
    // construction as the toolbar above (space for it already reserved out of the dockspace host
    // above). Rendered here, after DrawTopDownCanvas, so hoveredWorld reflects this same frame's
    // mouse position rather than lagging a frame behind. Always occupies its reserved space (so
    // the dockspace above never has to resize) -- when no timed showStatus() message is active,
    // it instead shows a live "X/Z under the cursor + what's selected" readout, normal idle state.
    ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x, mainViewport->WorkPos.y + mainViewport->WorkSize.y - statusBarHeight));
    ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, statusBarHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::Begin("##StatusBar", nullptr,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoSavedSettings);
    if (statusVisible) {
      ImGui::TextUnformatted(statusMessage.c_str());
    } else {
      char idleLine[256];
      if (hoveredWorld.has_value())
        std::snprintf(idleLine, sizeof(idleLine), "X: %.1f   Z: %.1f    %s", hoveredWorld->x, hoveredWorld->z, describeSelection().c_str());
      else
        std::snprintf(idleLine, sizeof(idleLine), "%s", describeSelection().c_str());
      ImGui::TextUnformatted(idleLine);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::Render();
    int drawableWidth = 0, drawableHeight = 0;
    SDL_GetWindowSizeInPixels(window, &drawableWidth, &drawableHeight);
    glViewport(0, 0, drawableWidth, drawableHeight);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DestroyContext(glContext);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
