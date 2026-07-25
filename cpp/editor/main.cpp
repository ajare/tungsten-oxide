// cpp/editor/main.cpp — track_editor: native ImGui/SDL2/OpenGL track editor.
// M0 (EDITOR_CPP_PORT_PLAN.md) proved the toolchain: window + one ImGui frame + core linked.
// M1 wires in the editor-owned authoring model (EditorTrackDefinition, undo/redo): this file
// builds an in-memory starter track, round-trips it through toJson/fromJson, hands it to
// tox::Track::fromJson for a live preview bake, and exercises one undo/redo step -- all shown in
// the ImGui window as a manual pass/fail smoke check. No point/mesh editing UI yet (M2+).
#include <cstdio>
#include <string>

#include "imconfig.h"  // pulls in the vendored gl3w loader (see imconfig.h)
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>

#include "EditorHistory.hpp"
#include "EditorTrackDefinition.hpp"
#include "Track.hpp"

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
      SDL_CreateWindow("track_editor (M1: authoring model)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800,
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

    ImGui::Begin("track_editor — M1: authoring model");
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
    ImGui::Separator();
    ImGui::TextUnformatted("Point/mesh editing UI lands in later EDITOR_CPP_PORT_PLAN.md milestones.");
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
