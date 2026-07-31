#define NOMINMAX

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

#include <mpp/ModelSerializer.h>
#include <mpp/ProgrammaticModelStream.h>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#pragma warning(pop)

#include <willpower/application/StateExceptions.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include "StatePlayTungstenMonoxide.h"
#include "Game.h"
#include "Map.h"
#include "ReactiveCamera.h"

using namespace std;
using namespace wp;

DisplayMessage::Level gDisplayMessageLevel = DisplayMessage::Level::Debug;

namespace {
constexpr double CAM_BACK = 13.0;
constexpr double CAM_ZOOM_MIN = 0.4;
constexpr double CAM_ZOOM_MAX = 3.0;
constexpr double CAM_UP_MIN = 0.5;
constexpr double CAM_UP_MAX = 25.0;
constexpr double LOOK_AT_FORWARD = 12.0;
constexpr double LOOK_AT_UP_MIN = -6.0;
constexpr double LOOK_AT_UP_MAX = 12.0;
constexpr double SHIP_CENTER_HEIGHT = 0.3;
constexpr double SHIP_BOB_AMPLITUDE = 0.06;

mpp::mesh::MeshSpecification shipMeshSpecification() {
  mpp::mesh::MeshSpecification spec(mpp::mesh::Primitive::Type::Triangles);
  auto layout = spec.createVertexBufferAttributeLayout(false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Position3, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Normal3, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::TexCoord2, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  spec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  spec.setIndexedVertices(true);
  return spec;
}

glm::vec3 toGlm(tox::Vec3 const& value) {
  return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}

string formatTime(double seconds) {
  seconds = max(0.0, seconds);
  int milliseconds = static_cast<int>(seconds * 1000.0) % 1000;
  int totalSeconds = static_cast<int>(seconds);
  ostringstream out;
  out << setfill('0') << setw(2) << totalSeconds / 60 << ':' << setw(2) << totalSeconds % 60 << '.' << setw(3) << milliseconds;
  return out.str();
}

void applyShipTransform(mpp::SceneModel3dPtr const& model, tox::Vec3 const& position,
                        tox::Vec3 const& upValue, tox::Vec3 const& forwardValue, double pitch, double bank) {
  tox::Vec3 up = tox::normalizeSafe(upValue);
  tox::Vec3 forward = forwardValue + up * -glm::dot(forwardValue, up);
  if (glm::dot(forward, forward) < 1e-9) forward = tox::Vec3(0, 0, 1);
  forward = tox::normalizeSafe(forward);
  tox::Vec3 right = tox::normalizeSafe(glm::cross(up, forward));
  forward = tox::normalizeSafe(glm::cross(right, up));

  glm::mat3 basis(toGlm(right), toGlm(up), toGlm(forward));
  glm::quat orientation = glm::quat_cast(basis);
  orientation *= glm::quat(glm::vec3(static_cast<float>(pitch), 0.0f, static_cast<float>(bank)));
  float angle = glm::angle(orientation);
  glm::vec3 axis = angle > 1e-6f ? glm::axis(orientation) : glm::vec3(0, 1, 0);

  model->resetTransform();
  model->translate(toGlm(position));
  model->rotateSelf(angle, axis);
  model->scale(glm::vec3(2.4f, 0.8f, 4.0f));
}
}  // namespace

StatePlayTungstenMonoxide::StatePlayTungstenMonoxide()
    : StatePlay(), mGlobalTime(0.0), mExitScheduled(false), mTrackModel(nullptr), mShipModel(nullptr) {
}

StatePlayTungstenMonoxide::~StatePlayTungstenMonoxide() = default;

Map* StatePlayTungstenMonoxide::getMap() { return static_cast<Map*>(mMap.get()); }
Map const* StatePlayTungstenMonoxide::getMap() const { return static_cast<Map const*>(mMap.get()); }

vector<string> StatePlayTungstenMonoxide::getDebuggingText() const {
  if (!mGameSession || mGameSession->ships().empty()) return {};
  auto const& ship = mGameSession->ships()[0];
  return {
      STR_FORMAT("Ship: {:.2f},{:.2f},{:.2f}", ship.physics.groundPos.x, ship.physics.groundPos.y, ship.physics.groundPos.z),
      STR_FORMAT("Speed: {:.1f} km/h", abs(ship.physics.speed) * 3.6),
      STR_FORMAT("Laps: {}  checkpoints: {}/{}", ship.race.laps, ship.race.hit.size(), ship.race.intermediateIds.size())};
}

void StatePlayTungstenMonoxide::createCamera() {
  float aspectRatio = mwRenderSystem->getWindowWidth() / static_cast<float>(mwRenderSystem->getWindowHeight());
  auto camera = new ReactiveCamera(glm::vec3(0, 0, 150), 180.0f, 0.0f, 65.0f, aspectRatio);
  camera->setClipDistances(0.2f, 2000.0f);
  mCamera3d = shared_ptr<mpp::Camera>(camera);
}

void StatePlayTungstenMonoxide::registerInput() {
  using namespace application;
  registerInputState("Exit", {Key::Escape}, {}, {}, {}, {}, {}, false, false, 0, true);
  // disableInGui=false: F1 must keep toggling the debug overlay even while it's open, or
  // there would be no way to close it again.
  registerInputState("ToggleTriggersDebug", {Key::F1}, {}, {}, {}, {}, {}, false, false, 0, false);
  registerInputState("DebugLaunch", {Key::J}, {}, {}, {}, {}, {}, false, false, 0, false);
  // InputStateManager treats every key in one definition as a chord (logical
  // AND), not alternatives. Register keyboard alternatives separately so W
  // works without also requiring UpArrow, and likewise for steering/braking.
  // disableInGui=false on every one of these: the framework's own guiActive gate only knows
  // whether the debug window is *open*, not whether it currently has mouse/keyboard focus, so
  // gating here would block driving any time the panel is open at all. Instead these stay live
  // and updateActions() itself drops them only while ImGui is actually capturing input this
  // frame (see guiCapturingInput there), so gameplay input bubbles through whenever the debug
  // window is open but unfocused.
  registerInputState("Accelerate", {}, {}, {Key::W}, {}, {}, {}, false, false, 0, false);
  registerInputState("AccelerateAlt", {}, {}, {Key::UpArrow}, {}, {}, {}, false, false, 0, false);
  registerInputState("Brake", {}, {}, {Key::S}, {}, {}, {}, false, false, 0, false);
  registerInputState("BrakeAlt", {}, {}, {Key::DownArrow}, {}, {}, {}, false, false, 0, false);
  registerInputState("SteerLeft", {}, {}, {Key::A}, {}, {}, {}, false, false, 0, false);
  registerInputState("SteerLeftAlt", {}, {}, {Key::LeftArrow}, {}, {}, {}, false, false, 0, false);
  registerInputState("SteerRight", {}, {}, {Key::D}, {}, {}, {}, false, false, 0, false);
  registerInputState("SteerRightAlt", {}, {}, {Key::RightArrow}, {}, {}, {}, false, false, 0, false);
  registerInputState("Respawn", {Key::R}, {}, {}, {}, {}, {}, false, false, 0, false);
}

mpp::ResourcePtr StatePlayTungstenMonoxide::createShipModel(
    application::resourcesystem::ResourceManager* resourceMgr, mpp::ResourceManager* renderResourceMgr) {
  auto game = static_pointer_cast<Game>(resourceMgr->getResource("TungstenMonoxide", ""));
  mpp::ModelSerializer serializer(renderResourceMgr);
  try {
    serializer.load(game->getShipModelPath());
  } catch (exception const& error) {
    throw application::resourcesystem::ResourceException(game.get(), "failed to load ShipModel: " + string(error.what()));
  }

  auto material = resourceMgr->getResource(game->getShipMaterial(), "");
  if (!material || material->getType() != "Material")
    throw application::resourcesystem::ResourceException(game.get(), "ShipModel Material is missing or is not a Material resource.");

  auto spec = shipMeshSpecification();
  auto stream = new mpp::ProgrammaticModelStream(renderResourceMgr);
  for (size_t i = 0; i < serializer.getMeshCount(); ++i) {
    if (serializer.getPrimitiveType(i) != mpp::mesh::Primitive::Type::Triangles)
      throw application::resourcesystem::ResourceException(game.get(), "ShipModel contains a non-triangle mesh.");
    int indexWidth = serializer.getIndexWidth(i);
    if (indexWidth != 16 && indexWidth != 32)
      throw application::resourcesystem::ResourceException(
          game.get(), "ShipModel must contain indexed meshes with 16- or 32-bit indices.");
    auto meshId = stream->createMesh(serializer.getName(i), spec, material->getQualifiedName(), indexWidth);
    size_t vertexCount, stride;
    shared_ptr<const int8_t> vertexData;
    serializer.getVertexStream(i, 0, &vertexCount, &stride, &vertexData);
    if (stride != spec.getVertexStrideInBytes())
      throw application::resourcesystem::ResourceException(game.get(), "ShipModel has an unsupported vertex layout.");
    stream->addVertexData(meshId, vector<int8_t>(vertexData.get(), vertexData.get() + vertexCount * stride));
    auto indices = serializer.getIndexData(i);
    for (int triangle = 0; triangle < serializer.getPrimitiveCount(i); ++triangle) {
      uint32_t corners[3];
      for (int corner = 0; corner < 3; ++corner) {
        const size_t index = static_cast<size_t>(triangle) * 3 + corner;
        if (indexWidth == 16) {
          uint16_t value;
          memcpy(&value, indices.get() + index * sizeof(value), sizeof(value));
          corners[corner] = value;
        } else {
          memcpy(&corners[corner], indices.get() + index * sizeof(corners[corner]),
                 sizeof(corners[corner]));
        }
        if (corners[corner] >= vertexCount)
          throw application::resourcesystem::ResourceException(
              game.get(), "ShipModel contains an out-of-range triangle index.");
      }
      stream->addTriangle(meshId, corners[0], corners[1], corners[2]);
    }
  }

  auto resource = renderResourceMgr->declareResource("TungstenMonoxide.ShipModel", mpp::ResourceStreamPtr(stream)).first;
  resource->acquire(&mWrangler);
  resource->load();
  return resource;
}

void StatePlayTungstenMonoxide::createGameObjects(
    application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem,
    mpp::ResourceManager* renderResourceMgr, void* args) {
  VAR_UNUSED(renderSystem);
  VAR_UNUSED(args);
  auto trackResource = resourceMgr->getResource("NewTrack", "Tracks");
  mTrackModel = trackResource->getMppResource();
  mTrackModel->acquire(&mWrangler);
  // Map::load only declares the render model because it can run on a worker
  // without an OpenGL context. MapLoad normally uploads it on the main thread;
  // keep this defensive for direct Play-state setup paths.
  if (!mTrackModel->isLoaded()) mTrackModel->load();
  mTrackSceneModel = mScene->add3dModel(mTrackModel);
  mTrackSceneModel->resetTransform();

  if (!getMap()->getTrack())
    throw application::resourcesystem::ResourceException(trackResource.get(), "Track resource has no compiled TrackData.");
  applyTriggersDebugVisibility();          // mShowTriggersDebug defaults to false: trigger quads start hidden.
  applyRailsDebugVisibility();             // mShowRailsDebug defaults to false: rails start hidden too.
  applyReservationWallsDebugVisibility();  // mShowReservationWallsDebug defaults to true: real gameplay geometry, starts visible.
  mGameSession = make_unique<tox::GameSession>(getMap()->getTrack(), tox::StartGrid::DEFAULT_SHIP_COUNT);
  auto const& poses = getMap()->getStartGridPoses();
  if (poses.size() != mGameSession->ships().size())
    throw application::resourcesystem::ResourceException(trackResource.get(), "starting-grid pose count does not match the game roster.");
  for (size_t i = 0; i < poses.size(); ++i) mGameSession->ships()[i].placeAt(mGameSession->simulation(), poses[i]);
  mInitialShipPhysics = mGameSession->ships()[0].physics;

  mShipModel = createShipModel(resourceMgr, renderResourceMgr);
  mShipSceneModels.reserve(mGameSession->ships().size());
  mShipVisualStates.resize(mGameSession->ships().size());
  for (size_t i = 0; i < mGameSession->ships().size(); ++i) {
    mShipSceneModels.push_back(mScene->add3dModel(mShipModel));
    auto const& physics = mGameSession->ships()[i].physics;
    mShipVisualStates[i].groundPos = physics.groundPos;
    mShipVisualStates[i].up = physics.up;
    mShipVisualStates[i].airborne = physics.airborne;
    applyShipTransform(mShipSceneModels[i], physics.groundPos + physics.up * 1.0,
                       physics.up, physics.forward, 0, 0);
  }
  applyWireframeDebug();  // mShowWireframeDebug defaults to false: everything starts shaded.
}

void StatePlayTungstenMonoxide::destroyGameObjects() {
  mGameSession.reset();
  mShipVisualStates.clear();
  for (auto const& model : mShipSceneModels) mScene->remove3dModel(model);
  mShipSceneModels.clear();
  if (mTrackSceneModel) mScene->remove3dModel(mTrackSceneModel);
  mTrackSceneModel.reset();
  if (mShipModel) mShipModel->release(&mWrangler);
  mShipModel.reset();
  if (mTrackModel) mTrackModel->release(&mWrangler);
  mTrackModel.reset();
}

void StatePlayTungstenMonoxide::setupEntityFacades() {}
void StatePlayTungstenMonoxide::setupEntities() {}

void StatePlayTungstenMonoxide::setupScene() {
  mScene->setClearColour(mpp::Colour::Black);
  updateShips(0.0f);
  updateChaseCamera(0.0f);
}

void StatePlayTungstenMonoxide::setup(
    application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem,
    mpp::ResourceManager* renderResourceMgr, void* args) {
  auto transitionData = static_cast<applib::StateTransitionData*>(args);
  mTransitionData.mapData.prevMap.map = transitionData->mapData.nextMap.map;
  mTransitionData.userData = transitionData->userData;
  mMap = transitionData->mapData.nextMap.map;
  createInput();
  createScreenFxManagement();
  createEntityManagement();
  createCamera();
  mEntityMgr->setRenderersVisible(false);
  loadAllReferencedResources();
  registerInput();
  createGameObjects(resourceMgr, renderSystem, renderResourceMgr, args);
  setupScene();
  transitionData->userData = nullptr;
}

void StatePlayTungstenMonoxide::updatePreInput(float frameTime) { VAR_UNUSED(frameTime); }
void StatePlayTungstenMonoxide::updatePreEntities(float frameTime) { VAR_UNUSED(frameTime); }
void StatePlayTungstenMonoxide::updateAudio(float frameTime) { VAR_UNUSED(frameTime); }

void StatePlayTungstenMonoxide::updatePostEntities(float frameTime) {
  updateShips(frameTime);
  if (mwAudioSystem) updateAudio(frameTime);
}

void StatePlayTungstenMonoxide::exit() { throw wp::application::ReturnFromStateException(&mTransitionData); }

void StatePlayTungstenMonoxide::updateActions(vector<string> const& activeStates, float frameTime) {
  if (!mGameSession) return;
  // Gameplay input states are registered with disableInGui=false (the framework's own gate only
  // knows whether the debug window is open, not whether it currently has focus), so filter them
  // here instead: only drop them while ImGui is actually capturing this frame's keyboard/mouse
  // (a widget hovered or focused), letting driving input bubble through whenever the panel is
  // open but unfocused. Guarded by mShowDebugUi since GetIO() needs a bound context, which is
  // only set up (in _renderImGui) on frames the panel actually renders.
  bool guiCapturingInput = mShowDebugUi && (ImGui::GetIO().WantCaptureKeyboard || ImGui::GetIO().WantCaptureMouse);
  const bool debugLaunchActive =
      find(activeStates.begin(), activeStates.end(), "DebugLaunch") != activeStates.end();
  tox::ControlIntent player;
  for (auto const& state : activeStates) {
    if (state == "Exit") {
      exit();
      continue;
    }
    if (state == "ToggleTriggersDebug") {
      mShowDebugUi = !mShowDebugUi;
      continue;
    }
    if (guiCapturingInput) continue;
    if (state == "Accelerate" || state == "AccelerateAlt")
      player.throttle = 1;
    else if (state == "Brake" || state == "BrakeAlt")
      player.brake = 1;
    else if (state == "SteerLeft" || state == "SteerLeftAlt")
      player.steer += 1;
    else if (state == "SteerRight" || state == "SteerRightAlt")
      player.steer -= 1;
    else if (state == "Respawn")
      player.respawn = true;
  }
  if (!guiCapturingInput && debugLaunchActive && !mDebugLaunchWasActive &&
      !mGameSession->ships().empty())
    mGameSession->simulation().launchShip(mGameSession->ships()[0],
                                          tox::Consts::MIN_LAUNCH_UPWARD_SPEED);
  mDebugLaunchWasActive = debugLaunchActive;

  vector<tox::ControlIntent> intents(mGameSession->ships().size());
  if (!intents.empty()) intents[0] = player;
  if (!mShipVisualStates.empty()) mShipVisualStates[0].steer = player.steer;
  mGameSession->step(intents, frameTime);
  for (auto const& event : mGameSession->events())
    if (event.shipIndex == 0 && event.type == tox::GameEventType::LapCompleted)
      mLapFlashUntil = mGameSession->sessionTime() + 0.5;
  mEntityMgr->setRenderersVisible(false);
}

void StatePlayTungstenMonoxide::updateShips(float frameTime) {
  if (!mGameSession) return;
  for (size_t i = 0; i < mGameSession->ships().size(); ++i) {
    auto const& physics = mGameSession->ships()[i].physics;
    auto& visual = mShipVisualStates[i];
    const bool landed = visual.airborne && !physics.airborne;
    if (landed) {
      // Bob is not applied in flight. Resume it from its neutral phase only
      // after landing so reattaching to a track or mesh cannot add an
      // unrelated sinusoidal position jump to the contact frame.
      visual.bobTime = 0.0;
      visual.landingBounce = 0.0;
      double impact = max(0.0, -visual.lastVerticalVelocity);
      // Apply the impact as spring velocity, not an immediate position offset.
      // The old displacement impulse could move the model several metres on
      // the exact frame that physics attached it to the surface.
      visual.landingBounceVel = min(16.0, impact * 0.35);
    }
    visual.airborne = physics.airborne;
    visual.lastVerticalVelocity = physics.verticalVel;

    double expectedStep = abs(physics.speed) * frameTime * 1.5 + 0.16;
    if (glm::distance(visual.groundPos, physics.groundPos) > expectedStep)
      visual.groundPos = glm::mix(visual.groundPos, physics.groundPos, min(1.0, frameTime * 18.0));
    else
      visual.groundPos = physics.groundPos;
    visual.up = tox::normalizeSafe(glm::mix(visual.up, physics.up, min(1.0, frameTime * 18.0)));
    if (!landed) {
      visual.landingBounceVel += -55.0 * visual.landingBounce * frameTime;
      visual.landingBounceVel *= exp(-7.0 * frameTime);
      visual.landingBounce += visual.landingBounceVel * frameTime;
    }
    if (!physics.airborne && !landed) visual.bobTime += frameTime;
    bool idle = i > 0 && !physics.airborne && abs(physics.speed) <= 0.001;
    const double bob = physics.airborne || idle
                           ? 0.0
                           : sin(visual.bobTime * 6.0) * SHIP_BOB_AMPLITUDE;
    double hover = idle ? 1.0 : 1.0 + bob + visual.landingBounce;
    double speedRatio = min(1.0, abs(physics.speed) / physics.maxSpeed);
    double targetBank = max(-0.5, min(0.5, -visual.steer * speedRatio * 0.5));
    visual.bank += (targetBank - visual.bank) * min(1.0, frameTime * 6.0);
    visual.pitch += (physics.speed * 0.004 - visual.pitch) * min(1.0, frameTime * 6.0);
    tox::Vec3 position = visual.groundPos + visual.up * hover;
    applyShipTransform(mShipSceneModels[i], position, visual.up, physics.forward, visual.pitch, visual.bank);
  }
}

void StatePlayTungstenMonoxide::updateChaseCamera(float frameTime) {
  VAR_UNUSED(frameTime);
  if (!mGameSession || mGameSession->ships().empty()) return;
  auto camera = static_cast<ReactiveCamera*>(mCamera3d.get());
  camera->setAspectRatio(mwRenderSystem->getWindowWidth() / static_cast<float>(mwRenderSystem->getWindowHeight()));
  auto const& physics = mGameSession->ships()[0].physics;
  auto const& visual = mShipVisualStates[0];
  tox::Vec3 up = visual.up;
  tox::Vec3 forward = tox::normalizeSafe(physics.forward + up * -glm::dot(physics.forward, up));
  tox::Vec3 center = visual.groundPos + up * SHIP_CENTER_HEIGHT;
  tox::Vec3 position = center + forward * (-CAM_BACK * mCameraZoom) + up * (mCameraHeight * mCameraZoom);
  tox::Vec3 lookAt = center + forward * LOOK_AT_FORWARD + up * mLookAtHeight;
  camera->setPosition(toGlm(position));
  camera->setOrientation(toGlm(tox::normalizeSafe(lookAt - position)), toGlm(up));
}

void StatePlayTungstenMonoxide::updateCamera(float frameTime) { updateChaseCamera(frameTime); }
void StatePlayTungstenMonoxide::updatePreRenderers(float frameTime) { VAR_UNUSED(frameTime); }
void StatePlayTungstenMonoxide::suspendImpl(void* args) { VAR_UNUSED(args); }

void StatePlayTungstenMonoxide::resumeImpl(void* args) {
  if (args) mExitScheduled = *static_cast<bool const*>(args);
}

void StatePlayTungstenMonoxide::updateImpl(float frameTime) {
  mGlobalTime += frameTime;
  if (mExitScheduled) exit();
  updatePreInput(frameTime);
  updateInput(frameTime);
  updatePreEntities(frameTime);
  updateEntityManagement(frameTime);
  updatePostEntities(frameTime);
  updateCamera(frameTime);
  updatePreRenderers(frameTime);
  updateScreenFxManagement(frameTime);
  updateRenderers(frameTime);
}

void StatePlayTungstenMonoxide::renderHud(mpp::RenderSystem* renderSystem) const {
  if (!mGameSession || mGameSession->ships().empty()) return;
  auto const& ship = mGameSession->ships()[0];
  bool flashing = mGameSession->sessionTime() < mLapFlashUntil;
  string checkpoints = "Checkpoints ";
  for (auto const& id : ship.race.intermediateIds) checkpoints += (flashing || ship.race.hit.count(id)) ? "[X] " : "[ ] ";
  vector<string> raceText{
      checkpoints,
      "Laps  " + to_string(ship.race.laps),
      "Lap   " + formatTime(mGameSession->sessionTime() - ship.race.lapStartedAt),
      "Total " + formatTime(mGameSession->sessionTime() - ship.race.totalStartedAt)};
  renderSystem->renderText(raceText, 16, 16, mpp::Colour::White);
  string speed = to_string(static_cast<int>(round(abs(ship.physics.speed) * 3.6))) + " km/h";
  if (ship.physics.boostActive) speed += "  BOOST";
  int speedX = max(16, static_cast<int>(renderSystem->getWindowWidth()) - 180);
  int speedY = max(16, static_cast<int>(renderSystem->getWindowHeight()) - 40);
  renderSystem->renderText(speed, speedX, speedY,
                           ship.physics.boostActive ? mpp::Colour::Yellow : mpp::Colour::White);
}

void StatePlayTungstenMonoxide::renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(resourceMgr);
  renderSystem->setAmbientColour(mpp::Colour::Grey25);
  renderSystem->setLightCount(1);
  renderSystem->setLight1Colour(mpp::Colour::White);
  renderSystem->renderScene(mScene, mCamera3d, {0.0f, 0.0f}, getName());
  renderHud(renderSystem);
}

// Trigger quads and rail walls are baked into the track model as individually-named meshes
// (TrackBake.cpp/TrackMesh.cpp's GeometryKind::TriggerSurface/PathRail/MeshRail, carried through
// Map::load() with GeometryBatch::id preserved verbatim as the mesh name), so showing/hiding a
// whole geometry kind is just a per-mesh render-flag toggle -- no separate geometry to draw.
// Folds in mShowWireframeDebug too: these are explicit per-mesh overrides (ModelInstance::setParams
// only falls back to the model-level default for a mesh with no entry of its own), so a kind
// toggled through here would otherwise keep whatever wireframe bit it last had regardless of the
// debug window's Wireframe checkbox.
void StatePlayTungstenMonoxide::setGeometryKindVisible(tox::GeometryKind kind, bool visible) const {
  if (!mTrackSceneModel || !getMap() || !getMap()->getTrack()) return;
  auto params = mTrackSceneModel->getParams();
  uint32_t flags = (visible ? mpp::ModelRenderParams::Flag_Visible : 0) |
                   (mShowWireframeDebug ? mpp::ModelRenderParams::Flag_Wireframe : 0);
  for (auto const& batch : getMap()->getTrack()->geometry)
    if (batch.kind == kind) params->setMeshFlags(batch.id, flags);
}

void StatePlayTungstenMonoxide::applyTriggersDebugVisibility() const {
  setGeometryKindVisible(tox::GeometryKind::TriggerSurface, mShowTriggersDebug);
}

void StatePlayTungstenMonoxide::applyRailsDebugVisibility() const {
  setGeometryKindVisible(tox::GeometryKind::PathRail, mShowRailsDebug);
  setGeometryKindVisible(tox::GeometryKind::MeshRail, mShowRailsDebug);
}

void StatePlayTungstenMonoxide::applyReservationWallsDebugVisibility() const {
  setGeometryKindVisible(tox::GeometryKind::ReservationWall, mShowReservationWallsDebug);
}

// Everything else -- the drivable road/mesh surfaces and shells, which never get an explicit
// per-mesh override above -- picks up wireframe through the model-level default instead (the "" key
// ModelInstance::setParams falls back to for any mesh without its own entry). Ship models have no
// per-mesh overrides at all, so the model-level default covers them outright. Re-running the three
// debug-visibility applies keeps their explicit overrides (trigger quads, rails, reservation walls)
// in sync with the current wireframe bit too, rather than only picking it up on their own next
// toggle.
void StatePlayTungstenMonoxide::applyWireframeDebug() const {
  uint32_t const wireframeBit = mShowWireframeDebug ? mpp::ModelRenderParams::Flag_Wireframe : 0;
  if (mTrackSceneModel) mTrackSceneModel->getParams()->setModelFlags(mpp::ModelRenderParams::Flag_Visible | wireframeBit);
  applyTriggersDebugVisibility();
  applyRailsDebugVisibility();
  applyReservationWallsDebugVisibility();
  for (auto const& model : mShipSceneModels) model->getParams()->setModelFlags(mpp::ModelRenderParams::Flag_Visible | wireframeBit);
}

// One slider + Reset button for a single physics field, ranged to +-20% of `initial` (the
// value captured before any debug edits). initial may be negative (e.g. maxReverse), so the
// smaller of the two scaled endpoints isn't necessarily the low end -- min/max sorts them.
void StatePlayTungstenMonoxide::renderPhysicsSlider(char const* label, double& value, double initial) const {
  double lo = std::min(initial * 0.8, initial * 1.2);
  double hi = std::max(initial * 0.8, initial * 1.2);
  ImGui::PushID(label);
  ImGui::SliderScalar(label, ImGuiDataType_Double, &value, &lo, &hi, "%.2f");
  ImGui::SameLine();
  if (ImGui::Button("Reset")) value = initial;
  ImGui::PopID();
}

void StatePlayTungstenMonoxide::renderShipPhysicsTab() const {
  if (!mGameSession || mGameSession->ships().empty()) return;
  tox::Physics& physics = mGameSession->ships()[0].physics;
  renderPhysicsSlider("Max Speed", physics.maxSpeed, mInitialShipPhysics.maxSpeed);
  renderPhysicsSlider("Max Reverse", physics.maxReverse, mInitialShipPhysics.maxReverse);
  renderPhysicsSlider("Accel", physics.accel, mInitialShipPhysics.accel);
  renderPhysicsSlider("Brake Decel", physics.brakeDecel, mInitialShipPhysics.brakeDecel);
  renderPhysicsSlider("Friction", physics.friction, mInitialShipPhysics.friction);
  renderPhysicsSlider("Turn Rate", physics.turnRate, mInitialShipPhysics.turnRate);
  renderPhysicsSlider("Weight", physics.weight, mInitialShipPhysics.weight);
}

bool StatePlayTungstenMonoxide::_imGuiActive() const { return mShowDebugUi; }

void StatePlayTungstenMonoxide::_renderImGui(float frameTime, void* imGuiCtx, void* imPlotCtx, void* allocFunc,
                                             void* freeFunc, void* userData) {
  VAR_UNUSED(frameTime);
  VAR_UNUSED(imPlotCtx);
  // ImGui statics aren't shared across the DLL boundary between this module and the launcher
  // that owns the context, so both must be rebound before any ImGui:: call.
  ImGui::SetCurrentContext(static_cast<ImGuiContext*>(imGuiCtx));
  ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(allocFunc), reinterpret_cast<ImGuiMemFreeFunc>(freeFunc), userData);

  ImGui::Begin("Debug");
  if (ImGui::BeginTabBar("DebugTabs")) {
    if (ImGui::BeginTabItem("Debug")) {
      if (ImGui::Checkbox("Show Triggers", &mShowTriggersDebug)) applyTriggersDebugVisibility();
      if (ImGui::Checkbox("Show Rails", &mShowRailsDebug)) applyRailsDebugVisibility();
      if (ImGui::Checkbox("Show Reservation Walls", &mShowReservationWallsDebug)) applyReservationWallsDebugVisibility();
      if (ImGui::Checkbox("Wireframe", &mShowWireframeDebug)) applyWireframeDebug();
      ImGui::SliderScalar("Camera Zoom", ImGuiDataType_Double, &mCameraZoom, &CAM_ZOOM_MIN, &CAM_ZOOM_MAX, "%.2f");
      ImGui::SliderScalar("Camera Height", ImGuiDataType_Double, &mCameraHeight, &CAM_UP_MIN, &CAM_UP_MAX, "%.2f");
      ImGui::SliderScalar("Camera Aim Height", ImGuiDataType_Double, &mLookAtHeight, &LOOK_AT_UP_MIN, &LOOK_AT_UP_MAX, "%.2f");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Physics")) {
      renderShipPhysicsTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}
