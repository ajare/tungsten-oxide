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
// Default mode drives a ship through GameSession with a small fixed, deterministic input script (no
// real-time clock, no interactive input) and logs position/velocity/normal/airborne-state every
// frame to stdout, one line per frame, so two runs can be diffed byte-for-byte to confirm
// reproducibility.
//
// `--capture-trace` mode (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 7.1) instead drives a single Ship
// directly through Simulation::stepPhysics -- not GameSession::step, which sub-steps/clamps and
// collects gameplay events the golden-trace corpus's replayer (cpp/core/tests/parity_main.cpp) never
// exercises -- and dumps a full raw-track-shaped JSON trace (sourceTrack, initialState, per-step
// control/after/outcome, all matching parity_main.cpp's loadShip()/checkD() field-for-field) plus
// the built collision surface itself (`collisionTriangles`, `meshMode: true`), so parity can replay
// a mesh-mode trace with zero mpp/willpower/GL dependency of its own.
//
// Not track_runner: track_runner (cpp/app/main.cpp) deliberately leaves Track::collisionSurface
// null and only ever drives analytic-mode physics (see cpp/core/CLAUDE.md's "Limitations" section)
// -- this tool exists specifically to exercise mesh-mode, which requires a real collision BVH.
#if !defined(_WIN32)
#error "mesh_physics_diag requires the Windows WGL headless OpenGL context implementation."
#endif

#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
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

#include "nlohmann/json.hpp"

#include "GameSession.hpp"
#include "ShipFactory.hpp"
#include "Simulation.hpp"
#include "StartGrid.hpp"
#include "Ship.hpp"
#include "Track.hpp"
#include "TrackCollision.hpp"
#include "TrackCollisionBuild.h"

using nlohmann::json;
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

// Trace-capture-only input script (Milestone 7.1): the generic sinusoidal scriptedInput() above
// drifts too far laterally to actually reach either the DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 6.1
// tunnel/ramp validation fixture's narrower features by the time it arrives at them, so a captured
// trace using it would exercise nothing but flat road (verified: zero airborne frames). A brief
// wall-clip burst (as independently verified by hand during Milestone 6.2's investigation) was tried
// here too, but its residual lateral drift carried through to the ramp and launched the ship off its
// *side* instead of its crest -- the two scenarios don't compose cleanly back-to-back in one run.
// Straight down the center line is what Milestone 6.3's own verification used and is what's kept
// here: a clean drive through the tunnel, a clean ramp launch/arc/landing on the platform beyond the
// gap. The in-tunnel wall bounce already has its own dedicated regression coverage
// (cpp/core/tests/track_tests.cpp's hull-vs-wall scenarios) and doesn't need to be reproduced here
// too.
ControlIntent captureScriptedInput(int frame, double dt) {
  ControlIntent intent;
  intent.throttle = 1.0;
  (void)frame;
  (void)dt;
  intent.steer = 0.0;
  return intent;
}

// Milestone 8.2 end-to-end validation script: full throttle throughout. A small symmetric steer "S"
// (t in [0.4s, 0.55s] then the opposite sign in [0.55s, 0.7s], zero elsewhere) is a lane change, not
// a turn -- steer is an angular *rate* control (Ship.cpp), so a one-sided burst leaves a permanent
// heading offset that a return to steer=0 never corrects; the matched opposite pulse cancels the
// rotation it introduced, leaving the ship laterally offset but pointed straight down +Z again well
// before the tunnel (z=48) and ramp (z=85) -- confirmed clean: continuous ground contact throughout,
// the tunnel-boost zone and ramp-checkpoint trigger both fire, a clean ramp launch/arc/landing
// follows, same shape as Milestone 7.1's own straight-line capture. This script stays this gentle
// deliberately: a stronger burst aimed at actually contacting the central-reservation-replacement
// barrier (z=-85) was tried and does produce a real, clearly visible wall bounce (a discontinuous
// position/speed change consistent with weightRestitution) -- see the plan doc's Milestone 8.2 entry
// -- but the resulting heading disturbance is too large to also recover in time for a clean tunnel
// pass and ramp launch in the same take, the same composition problem 7.1 hit combining a wall clip
// with the ramp. The wall-bounce evidence lives in the plan doc as a separate one-off run instead,
// matching 7.1's precedent of not forcing every scenario into a single script.
ControlIntent validationScriptedInput(int frame, double dt) {
  ControlIntent intent;
  intent.throttle = 1.0;
  const double t = frame * dt;
  if (t >= 0.4 && t < 0.55)
    intent.steer = -0.35;
  else if (t >= 0.55 && t < 0.7)
    intent.steer = 0.35;
  else
    intent.steer = 0.0;
  return intent;
}

// Builds the collision surface exactly as Map::load() would, given an already-constructed
// mpp::ResourceManager/ModelSerializer. Shared by both this tool's modes.
bool buildCollisionSurface(const std::filesystem::path& trackPath, mpp::ResourceManager& resourceMgr, mpp::ModelSerializer& serializer,
                           Track& track, std::string& outError) {
  try {
    const std::vector<std::string> selectedNames = mono::collidableGeometryBatchIds(track);
    std::vector<CollisionTriangle> collisionTriangles = mono::buildCollisionTriangles(serializer, track, selectedNames);

    const std::filesystem::path root = trackPath.parent_path();
    int nextSurfaceId = static_cast<int>(selectedNames.size());
    std::map<std::string, std::shared_ptr<mono::MeshObjectModel>> modelCache;
    auto meshObjectTriangles = mono::buildMeshObjectCollisionTriangles(track, root, &resourceMgr, nextSurfaceId, modelCache);
    collisionTriangles.insert(collisionTriangles.end(), std::make_move_iterator(meshObjectTriangles.begin()),
                              std::make_move_iterator(meshObjectTriangles.end()));
    if (collisionTriangles.empty()) {
      outError = "track produced no collision triangles -- nothing to drive on";
      return false;
    }
    track.collisionSurface = std::make_shared<TrackCollisionSurface>(std::move(collisionTriangles));
    return true;
  } catch (std::exception const& error) {
    outError = std::string("failed to build collision surface: ") + error.what();
    return false;
  }
}

json dumpVec(const Vec3& v) {
  return json::array({v.x, v.y, v.z});
}

// Mirrors cpp/core/tests/parity_main.cpp's loadShip() field-for-field, in reverse -- see that
// function for the schema this must produce.
json dumpShip(const Ship& ship) {
  const Physics& p = ship.physics;
  json physics = {
      {"heading", p.heading}, {"speed", p.speed}, {"maxSpeed", p.maxSpeed}, {"maxReverse", p.maxReverse},
      {"accel", p.accel}, {"brakeDecel", p.brakeDecel}, {"friction", p.friction}, {"turnRate", p.turnRate},
      {"grip", p.grip}, {"wallRestitution", p.wallRestitution}, {"weight", p.weight}, {"bobTime", p.bobTime},
      {"visualBank", p.visualBank}, {"visualPitch", p.visualPitch}, {"airborne", p.airborne},
      {"verticalVel", p.verticalVel}, {"gravity", p.gravity}, {"landingBounce", p.landingBounce},
      {"landingBounceVel", p.landingBounceVel}, {"boostActive", p.boostActive}, {"boostReleasing", p.boostReleasing},
      {"boostHold", p.boostHold}, {"boostReleaseT", p.boostReleaseT}, {"boostCap", p.boostCap}, {"boostEffCap", p.boostEffCap},
      {"up", dumpVec(p.up)}, {"forward", dumpVec(p.forward)}, {"right", dumpVec(p.right)},
      {"groundPos", dumpVec(p.groundPos)}, {"visualGroundPos", dumpVec(p.visualGroundPos)}, {"visualUp", dumpVec(p.visualUp)},
      {"moveDir", dumpVec(p.moveDir)}};

  json zoneInside = json::array();
  for (const auto& [id, inside] : ship.zoneInside) zoneInside.push_back(json::array({id, inside}));
  json triggerStates = json::array();
  for (const auto& [id, state] : ship.triggerStates)
    triggerStates.push_back(json::array({id, json{{"armed", state.armed}, {"flash", state.flash}}}));

  json lastCheckpoint = {{"valid", ship.lastCheckpoint.valid},
                         {"triggerId", ship.lastCheckpoint.triggerId.empty() ? json(nullptr) : json(ship.lastCheckpoint.triggerId)},
                         {"pos", dumpVec(ship.lastCheckpoint.pos)},
                         {"forward", dumpVec(ship.lastCheckpoint.forward)},
                         {"up", dumpVec(ship.lastCheckpoint.up)}};

  json race = {{"laps", ship.race.laps},
              {"hit", ship.race.hit},
              {"intermediateIds", ship.race.intermediateIds},
              {"finishId", ship.race.finishId.empty() ? json(nullptr) : json(ship.race.finishId)}};

  json startPose = {{"pos", dumpVec(ship.startPose.pos)}, {"up", dumpVec(ship.startPose.up)}, {"forward", dumpVec(ship.startPose.forward)}};

  return json{{"physics", physics},          {"prevTriggerPos", dumpVec(ship.prevTriggerPos)}, {"zoneInside", zoneInside},
              {"triggerStates", triggerStates}, {"lastCheckpoint", lastCheckpoint},                {"race", race},
              {"startPose", startPose},
              // Absent from every pre-Milestone-7 (analytic-only) trace format -- see
              // cpp/core/tests/parity_main.cpp's loadShip() comment on why a mesh-mode trace needs
              // this restored explicitly, unlike everything else here which loadShip() already
              // round-trips.
              {"renderNormal", dumpVec(ship.renderNormal)}};
}

// Mirrors parity_main.cpp's surfaceLabel() exactly -- the trace's "outcome.surface" field must
// match what replay will independently recompute from the same Simulation/Track/Ship.
std::string surfaceLabel(const Simulation& simulation, const Ship& ship) {
  if (ship.physics.airborne) return "airborne";
  const Vec3& pos = ship.physics.groundPos;
  const Sample sample = simulation.sampleTrack(pos.x, pos.y, pos.z);
  return "path:" + std::to_string(sample.pathIndex);
}

int runCapture(const std::filesystem::path& outputPath, const std::string& traceName, const std::filesystem::path& trackPath,
              const std::filesystem::path& modelPath, int steps, double dt) {
  TrackLoadResult loaded = Track::fromFile(trackPath);
  if (!loaded) {
    std::cerr << "failed to load '" << trackPath.string() << "': " << loaded.error << "\n";
    return 1;
  }
  Track track = std::move(*loaded.track);

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

  std::string buildError;
  if (!buildCollisionSurface(trackPath, resourceMgr, serializer, track, buildError)) {
    std::cerr << buildError << "\n";
    return 1;
  }

  Simulation simulation(track);
  simulation.setMeshPhysicsEnabled(true);
  const std::vector<Pose> gridPoses = StartGrid::startingGridPoses(simulation, track, 1);
  if (gridPoses.empty()) {
    std::cerr << "could not compute a starting-grid pose\n";
    return 1;
  }
  Ship ship = ShipFactory::makeShip(simulation, track, gridPoses.front());

  std::ifstream sourceTrackFile(trackPath, std::ios::binary);
  json sourceTrackJson;
  sourceTrackFile >> sourceTrackJson;

  json collisionTrianglesJson = json::array();
  for (const CollisionTriangle& triangle : track.collisionSurface->triangles()) {
    collisionTrianglesJson.push_back(json{{"positions", json::array({dumpVec(triangle.positions[0]), dumpVec(triangle.positions[1]),
                                                                      dumpVec(triangle.positions[2])})},
                                          {"normals", json::array({dumpVec(triangle.normals[0]), dumpVec(triangle.normals[1]),
                                                                    dumpVec(triangle.normals[2])})},
                                          {"surfaceId", triangle.surfaceId}});
  }

  json trace;
  trace["meta"] = {{"name", traceName}, {"kind", "raw-track"}, {"steps", steps}};
  trace["sourceTrack"] = sourceTrackJson;
  trace["meshMode"] = true;
  trace["collisionTriangles"] = collisionTrianglesJson;
  trace["initialState"] = dumpShip(ship);

  json stepsArr = json::array();
  for (int frame = 0; frame < steps; ++frame) {
    const ControlIntent intent = captureScriptedInput(frame, dt);
    const StepResult result = simulation.stepPhysics(ship, dt, intent.throttle, intent.brake, intent.steer);
    // Simulation::stepPhysics(), unlike GameSession::step(), never maintains ship.renderNormal
    // itself -- Ship.hpp documents that as GameSession's job (see GameSession.cpp's own `if
    // (!r.respawned) ship.renderNormal = r.surfaceNormal;`), but stepMeshPhysics's probeAxis reads
    // it as a real physics input (steering/moveDir tangentize against it), not just a cosmetic
    // render value. Skipping this left every direct stepPhysics() caller silently probing/steering
    // against a stale (0,1,0) axis forever on any non-flat mesh surface -- reproduced headlessly: a
    // ship correctly climbing DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 6.1's ramp asset (per its own
    // groundPos.y) never launched off its crest the way the exact same drive via GameSession does,
    // because vel.y kept getting flattened back to 0 by this stale-axis tangentize every frame
    // before Ship.cpp's ramp-launch check ever saw it. Mirrored here to match GameSession's
    // contract; cpp/core/tests/parity_main.cpp's replay needs the identical fix to stay consistent
    // with what this capture records (see its own comment on this).
    if (!result.respawned) ship.renderNormal = result.surfaceNormal;
    stepsArr.push_back(json{
        {"control", {{"throttle", intent.throttle}, {"brake", intent.brake}, {"steer", intent.steer}, {"dt", dt}}},
        {"after", dumpShip(ship)},
        {"outcome", {{"surface", surfaceLabel(simulation, ship)}, {"railHit", result.railHit}, {"respawned", result.respawned}}}});
  }
  trace["steps"] = stepsArr;

  std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
  out << trace.dump();
  out.close();
  std::cout << "wrote " << steps << " step(s), " << collisionTrianglesJson.size() << " collision triangle(s) to " << outputPath.string()
            << "\n";
  return 0;
}

// Mirrors GameEventType (GameSession.hpp) as a short label for logging -- kept local since this
// tool only ever prints events, never branches on the enum value itself.
const char* eventLabel(GameEventType type) {
  switch (type) {
    case GameEventType::TriggerFired:
      return "TriggerFired";
    case GameEventType::CheckpointAccepted:
      return "CheckpointAccepted";
    case GameEventType::LapCompleted:
      return "LapCompleted";
    case GameEventType::Respawned:
      return "Respawned";
    case GameEventType::RailHit:
      return "RailHit";
  }
  return "Unknown";
}

int runDrive(const std::filesystem::path& trackPath, const std::filesystem::path& modelPath, int steps, double dt,
             ControlIntent (*script)(int, double) = scriptedInput) {
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

  std::string buildError;
  if (!buildCollisionSurface(trackPath, resourceMgr, serializer, *track, buildError)) {
    std::cerr << buildError << "\n";
    return 1;
  }

  GameSession session(track);
  session.setMeshPhysicsEnabled(true);
  std::cout << "# loaded '" << track->definition.name << "': " << track->paths.size() << " path(s), "
            << session.ships().size() << " ship(s), " << track->collisionSurface->triangles().size() << " collision triangle(s)\n";
  std::cout << "# frame t pos.x pos.y pos.z speed normal.x normal.y normal.z airborne\n";

  const std::vector<ControlIntent> idleIntents(session.ships().size());
  bool wasBoostActive = false;
  for (int frame = 0; frame < steps; ++frame) {
    std::vector<ControlIntent> intents = idleIntents;
    intents[0] = script(frame, dt);
    session.step(intents, dt);

    const Ship& ship0 = session.ships()[0];
    const Physics& p = ship0.physics;
    std::printf("%d %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %d\n", frame, session.sessionTime(), p.groundPos.x, p.groundPos.y, p.groundPos.z,
                p.speed, ship0.renderNormal.x, ship0.renderNormal.y, ship0.renderNormal.z, p.airborne ? 1 : 0);
    for (const GameEvent& event : session.events()) {
      if (event.shipIndex != 0) continue;
      std::printf("# EVENT frame=%d %s trigger=%s dir=%s auto=%d\n", frame, eventLabel(event.type), event.triggerId.c_str(),
                  event.direction.c_str(), event.automatic ? 1 : 0);
    }
    // Zones (unlike triggers) never raise a GameEvent -- triggerBoost (Simulation.cpp) mutates
    // Physics::boostActive directly -- so a boost zone's firing is only observable by watching that
    // flag's edge here.
    if (p.boostActive && !wasBoostActive) std::printf("# EVENT frame=%d ZoneBoostActivated\n", frame);
    wasBoostActive = p.boostActive;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "--capture-trace") {
    if (argc < 6) {
      std::cerr << "usage: mesh_physics_diag --capture-trace <output.json> <trace-name> <track.json> <model.mppmodel> [steps] [dt]\n";
      return 2;
    }
    const std::filesystem::path outputPath(argv[2]);
    const std::string traceName = argv[3];
    const std::filesystem::path trackPath(argv[4]);
    const std::filesystem::path modelPath(argv[5]);
    const int steps = argc > 6 ? std::atoi(argv[6]) : 600;
    const double dt = argc > 7 ? std::atof(argv[7]) : 1.0 / 60.0;
    return runCapture(outputPath, traceName, trackPath, modelPath, steps, dt);
  }

  // DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 8.2's end-to-end validation script -- see
  // validationScriptedInput's comment.
  if (argc >= 2 && std::string(argv[1]) == "--validate") {
    if (argc < 4) {
      std::cerr << "usage: mesh_physics_diag --validate <track.json> <model.mppmodel> [steps] [dt]\n";
      return 2;
    }
    const std::filesystem::path trackPath(argv[2]);
    const std::filesystem::path modelPath(argv[3]);
    const int steps = argc > 4 ? std::atoi(argv[4]) : 600;
    const double dt = argc > 5 ? std::atof(argv[5]) : 1.0 / 60.0;
    return runDrive(trackPath, modelPath, steps, dt, validationScriptedInput);
  }

  if (argc < 3) {
    std::cerr << "usage: mesh_physics_diag <track.json> <model.mppmodel> [steps] [dt]\n";
    std::cerr << "       mesh_physics_diag --capture-trace <output.json> <trace-name> <track.json> <model.mppmodel> [steps] [dt]\n";
    std::cerr << "       mesh_physics_diag --validate <track.json> <model.mppmodel> [steps] [dt]\n";
    return 2;
  }
  const std::filesystem::path trackPath(argv[1]);
  const std::filesystem::path modelPath(argv[2]);
  const int steps = argc > 3 ? std::atoi(argv[3]) : 600;
  const double dt = argc > 4 ? std::atof(argv[4]) : 1.0 / 60.0;
  return runDrive(trackPath, modelPath, steps, dt);
}
