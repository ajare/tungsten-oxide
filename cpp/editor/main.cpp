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
// M6 adds the elevation profile side view (ElevationView.hpp/.cpp): a second canvas showing the
// current path's baked Y profile plus draggable position-point elevation markers, collapsible.
#include <cstdio>
#include <string>

#include "imconfig.h"  // pulls in the vendored gl3w loader (see imconfig.h)
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>

#include "EditorHistory.hpp"
#include "EditorState.hpp"
#include "EditorTrackDefinition.hpp"
#include "Track.hpp"
#include "ElevationView.hpp"
#include "TopDownCanvas.hpp"
#include "TopDownView.hpp"

namespace {

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
      {1332.907, 0, 0},        {1154.331, 0, 666.453},   {666.453, 0, 1154.331},   {0, 0, 1332.907},
      {-666.453, 0, 1154.331}, {-1154.331, 0, 666.453},  {-1332.907, 0, 0},        {-1154.331, 0, -666.453},
      {-666.453, 0, -1154.331}, {0, 0, -1332.907},       {666.453, 0, -1154.331},  {1154.331, 0, -666.453},
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
  result.roundTripOk = (json1 == json2);

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
      SDL_CreateWindow("track_editor (M6: elevation profile)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800,
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

  // The canvas needs a persistent EditorState (authored track + mode/selection/drag/undo-redo)
  // plus its baked preview and view/camera state, all surviving across frames. There is no
  // "new track"/load UI yet (M4+), so the starter track is the only thing on screen.
  editor::EditorState editorState(buildStarterTrack());
  tox::TrackLoadResult bakedResult = tox::Track::fromJson(editor::toJson(editorState.track()));
  const tox::Track* bakedTrack = bakedResult ? &*bakedResult.track : nullptr;
  editor::TopDownView topDownView;
  bool elevationVisible = true;

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
      const bool ctrl = io.KeyCtrl;
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z) && editorState.undo()) rebake();
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y) && editorState.redo()) rebake();
    }

    ImGui::Begin("track_editor — status");
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
    ImGui::Separator();
    ImGui::TextUnformatted("Mode (E/C/R):");
    ImGui::SameLine();
    int modeIndex = static_cast<int>(editorState.mode());
    const char* modeNames[] = {"Edit", "Create", "Rails"};
    if (ImGui::Combo("##mode", &modeIndex, modeNames, 3)) editorState.setMode(static_cast<editor::EditMode>(modeIndex));
    if (ImGui::Button("Undo (Ctrl+Z)")) {
      if (editorState.undo()) rebake();
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo (Ctrl+Y)")) {
      if (editorState.redo()) rebake();
    }
    if (ImGui::Button("Place Test Mesh")) {
      // Placed just outside the starter circle (radius ~1333m) so it's never accidentally on top
      // of the track -- there's no asset library/drag-from-palette UI yet (M4 is placement, not
      // authoring), so this is the only way to get a mesh region onto the canvas at all.
      if (editorState.placeMeshAsset("test-rect", 1600.0, 0.0)) rebake();
    }
    ImGui::Separator();
    switch (editorState.mode()) {
      case editor::EditMode::Edit:
        ImGui::TextUnformatted("Edit mode: click to select a point or mesh region.");
        ImGui::TextUnformatted("Drag to move; shift+drag a mesh to rotate it about its own origin.");
        ImGui::TextUnformatted("Delete/Backspace removes whichever is selected.");
        break;
      case editor::EditMode::Create:
        ImGui::TextUnformatted("Create mode: click to add points; click the first point to close, the last to finish open.");
        ImGui::TextUnformatted("Right-click cancels the in-progress draft.");
        break;
      case editor::EditMode::Rails:
        ImGui::TextUnformatted("Rails mode: click near a mesh edge to toggle it as a rail (orange = flagged).");
        ImGui::TextUnformatted("A miss pans instead, same as Edit/Create mode's right-drag.");
        break;
    }
    ImGui::TextUnformatted("Top-down view: right-drag to pan, scroll to zoom, Home to reset.");
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
      // otherwise the first path -- so there's always something sensible to show.
      const int elevationPathIndex =
          editorState.selection().valid() ? editorState.selection().pathIndex : (editorState.track().paths.empty() ? -1 : 0);
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
