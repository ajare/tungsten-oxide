// mesh_physics_diag.cpp — permanent headless mesh-mode physics diagnostic tool
// (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 6.0). Loads a real track resource (TrackData JSON +
// ModelFile .mppmodel) exactly as tungsten-monoxide's Map::load() does -- same
// TrackCollisionBuild.h/.cpp BVH-construction code, same collision surface -- with no willpower
// resource-system dependency and no *visible* window/interaction: mpp::RenderSystem and
// mpp::ResourceManager are constructed with dummy 1x1 dimensions purely so mpp::ModelSerializer has
// somewhere to point, and nothing here ever renders a frame or uploads a real GPU resource. It does
// still need one real (invisible) OpenGL context, though -- see createHeadlessGLContext() below for
// why that turned out not to be avoidable, unlike the rest of the willpower/mpp resource system.
//
// Drives a ship through GameSession with a small fixed, deterministic input script (no real-time
// clock, no interactive input) and logs position/velocity/normal/airborne-state every frame to
// stdout, one line per frame, so two runs can be diffed byte-for-byte to confirm reproducibility.
//
// Not track_runner: track_runner (cpp/app/main.cpp) deliberately leaves Track::collisionSurface
// null and only ever drives analytic-mode physics (see cpp/core/CLAUDE.md's "Limitations" section)
// -- this tool exists specifically to exercise mesh-mode, which requires a real collision BVH.
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <mpp/Logger.h>
#include <mpp/ModelSerializer.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include "GameSession.hpp"
#include "Ship.hpp"
#include "Track.hpp"
#include "TrackCollision.hpp"
#include "TrackCollisionBuild.h"

using namespace tox;

namespace {

#ifdef _WIN32
// mpp::RenderSystem's constructor unconditionally calls glewInit() and issues a handful of GL
// calls (setDefaultState/createLightsData/glGenBuffers) even though this tool never renders
// anything -- there is no lazy/deferred GL init path to skip. glewInit() itself requires a current,
// real OpenGL context (not just a loaded opengl32.dll) or it crashes resolving function pointers
// that were never bound. So "headless" here means no *visible* window and no render/interaction
// loop, not literally no GL context: this creates one throwaway 1x1 hidden window + legacy WGL
// context purely so glewInit() has something to bind to, then leaks both deliberately (a
// short-lived CLI process; the OS reclaims them at exit, and there is no matching teardown call
// anywhere else in this tool to bother pairing it with).
bool createHeadlessGLContext() {
  WNDCLASSA wc{};
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "mesh_physics_diag_hidden_gl";
  if (!RegisterClassA(&wc)) return false;

  HWND hwnd = CreateWindowA(wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
  if (hwnd == nullptr) return false;
  HDC hdc = GetDC(hwnd);

  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  const int pixelFormat = ChoosePixelFormat(hdc, &pfd);
  if (pixelFormat == 0 || !SetPixelFormat(hdc, pixelFormat, &pfd)) return false;

  HGLRC hglrc = wglCreateContext(hdc);
  if (hglrc == nullptr) return false;
  return wglMakeCurrent(hdc, hglrc) == TRUE;
}
#endif

// Small deterministic input script: full throttle, a slow sinusoidal steer sweep so the ship
// wanders laterally across the drivable surface (and, once a Milestone 6.1 asset places a tunnel/
// loop/overhang in its path, across that geometry too) rather than driving a single straight line.
// Never reads a clock -- purely a function of frame index -- so a byte-identical rerun is
// guaranteed by construction, not just empirically likely.
ControlIntent scriptedInput(int frame, double dt) {
  ControlIntent intent;
  intent.throttle = 1.0;
  const double t = frame * dt;
  intent.steer = std::sin(t * 0.35) * 0.6;
  return intent;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: mesh_physics_diag <track.json> <model.mppmodel> [steps] [dt]\n";
    return 2;
  }

  const std::filesystem::path trackPath(argv[1]);
  const std::filesystem::path modelPath(argv[2]);
  const int steps = argc > 3 ? std::atoi(argv[3]) : 600;
  const double dt = argc > 4 ? std::atof(argv[4]) : 1.0 / 60.0;

  TrackLoadResult loaded = Track::fromFile(trackPath);
  for (const TrackWarning& warning : loaded.warnings) {
    std::cerr << "warning [" << warning.code << "] " << warning.message;
    if (!warning.objectId.empty()) std::cerr << " (" << warning.objectId << ")";
    std::cerr << "\n";
  }
  if (!loaded) {
    std::cerr << "failed to load '" << trackPath.string() << "': " << loaded.error << "\n";
    return 1;
  }
  auto track = std::make_shared<Track>(std::move(*loaded.track));

  // Dummy render/resource plumbing -- see createHeadlessGLContext()'s comment above for why the GL
  // context is unavoidable; nothing here ever renders or uploads a GPU resource.
#ifdef _WIN32
  if (!createHeadlessGLContext()) {
    std::cerr << "failed to create the throwaway hidden GL context mpp::RenderSystem's constructor requires\n";
    return 1;
  }
#endif

  mpp::Logger logger;
  logger.initialise("mesh_physics_diag.log", mpp::Logger::Level::Info);
  mpp::RenderSystem renderSystem(1, 1, &logger);
  mpp::ResourceManager resourceMgr(&renderSystem, &logger);

  mpp::ModelSerializer serializer(&resourceMgr);
  try {
    serializer.load(modelPath.string());
  } catch (std::exception const& error) {
    std::cerr << "failed to load ModelFile '" << modelPath.string() << "': " << error.what() << "\n";
    return 1;
  }

  std::vector<CollisionTriangle> collisionTriangles;
  try {
    const std::vector<std::string> selectedNames = mono::collidableGeometryBatchIds(*track);
    collisionTriangles = mono::buildCollisionTriangles(serializer, *track, selectedNames);

    const std::filesystem::path root = trackPath.parent_path();
    int nextSurfaceId = static_cast<int>(selectedNames.size());
    std::map<std::string, std::shared_ptr<mono::MeshObjectModel>> modelCache;
    auto meshObjectTriangles = mono::buildMeshObjectCollisionTriangles(*track, root, &resourceMgr, nextSurfaceId, modelCache);
    collisionTriangles.insert(collisionTriangles.end(), std::make_move_iterator(meshObjectTriangles.begin()),
                              std::make_move_iterator(meshObjectTriangles.end()));
  } catch (std::exception const& error) {
    std::cerr << "failed to build collision surface: " << error.what() << "\n";
    return 1;
  }
  if (collisionTriangles.empty()) {
    std::cerr << "track produced no collision triangles -- nothing to drive on\n";
    return 1;
  }
  track->collisionSurface = std::make_shared<TrackCollisionSurface>(std::move(collisionTriangles));

  GameSession session(track);
  session.setMeshPhysicsEnabled(true);
  std::cout << "# loaded '" << track->definition.name << "': " << track->paths.size() << " path(s), "
            << session.ships().size() << " ship(s), " << track->collisionSurface->triangles().size() << " collision triangle(s)\n";
  std::cout << "# frame t pos.x pos.y pos.z speed normal.x normal.y normal.z airborne\n";

  const std::vector<ControlIntent> idleIntents(session.ships().size());
  for (int frame = 0; frame < steps; ++frame) {
    std::vector<ControlIntent> intents = idleIntents;
    intents[0] = scriptedInput(frame, dt);
    session.step(intents, dt);

    const Ship& ship0 = session.ships()[0];
    const Physics& p = ship0.physics;
    std::printf("%d %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %d\n", frame, session.sessionTime(), p.groundPos.x, p.groundPos.y, p.groundPos.z,
                p.speed, ship0.renderNormal.x, ship0.renderNormal.y, ship0.renderNormal.z, p.airborne ? 1 : 0);
  }

  return 0;
}
