#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <mutex>

#include <mpp/Camera.h>
#include <mpp/ResourceWrangler.h>
#include <mpp/SceneModel3d.h>

#include <willpower/application/StateFactory.h>

#include <willpower/common/AccelerationGrid.h>

#include <applib/StatePlay.h>

#include "imgui/imgui.h"

#include "Platform.h"
#include "Map.h"
#include "DisplayMessage.h"
#include "GameSession.hpp"

class TungstenPbrPackage;

class TmResourceWrangler : public mpp::ResourceWrangler {
public:
  TmResourceWrangler()
      : ResourceWrangler("TungstenMonoxidePlay") {
  }
};

class APPLICATION_API StatePlayTungstenMonoxide : public applib::StatePlay {
  double mGlobalTime;

  mpp::CameraPtr mCamera3d;

  bool mExitScheduled;

  TmResourceWrangler mWrangler;

  mpp::ResourcePtr mTrackModel;
  mpp::ResourcePtr mShipModel;
  mpp::SceneModel3dPtr mTrackSceneModel;
  std::vector<mpp::SceneModel3dPtr> mShipSceneModels;
  std::unique_ptr<tox::GameSession> mGameSession;
  // "Show Physics Ghost" debug feature: a second instance of ship 0's model, driven by
  // GameSession::stepGhost with whichever physics mode ISN'T currently active, so the two methods'
  // predicted positions can be compared live. Shares mShipModel (no separate resource), rendered
  // wireframe-only (the renderer has no translucency primitive to give it a true "ghost" look --
  // see docs/MESH_PHYSICS_PLAN.md).
  mpp::SceneModel3dPtr mGhostShipSceneModel;
  tox::Ship mGhostShip;

  struct ShipVisualState {
    tox::Vec3 groundPos;
    tox::Vec3 up{0, 1, 0};
    double bobTime{0};
    double landingBounce{0};
    double landingBounceVel{0};
    double bank{0};
    double pitch{0};
    double steer{0};
    bool airborne{false};
    double lastVerticalVelocity{0};
  };
  std::vector<ShipVisualState> mShipVisualStates;
  double mCameraZoom{1.0};
  double mCameraHeight{6.4};
  double mLookAtHeight{1.6};
  double mLapFlashUntil{0.0};
  std::uint32_t mPbrViewportWidth{0};
  std::uint32_t mPbrViewportHeight{0};
  bool mPbrRenderValidated{false};

  bool mShowDebugUi{false};
  // Every warning collected during this session's Map::load() (Map::loadWarnings()), copied out in
  // createGameObjects() once loading finishes -- e.g. a drivable mesh object sub-mesh skipped for
  // an unresolved material (the "ModelTool.DefaultFallbackMaterial3D" case). Surfaced to the player
  // via a dismissible ImGui modal in _renderImGui, independent of mShowDebugUi (a player who never
  // opens the debug panel should still see that part of the map failed to load), not just logged.
  std::vector<std::string> mMapLoadWarnings;
  bool mMapLoadWarningsDialogOpen{false};
  bool mDebugLaunchWasActive{false};
  bool mShowTriggersDebug{false};
  bool mShowRailsDebug{false};
  // Unlike the debug-only Trigger/Rail visuals above, a central-reservation wall is real gameplay
  // geometry (CENTRAL_RESERVATION_PLAN.md) -- it defaults to visible; this checkbox lets it be
  // hidden for debugging (e.g. to see the physics wall's alignment without the mesh in the way).
  bool mShowReservationWallsDebug{true};
  bool mShowWireframeDebug{false};
  // Live toggle for Simulation::meshPhysicsEnabled (docs/MESH_PHYSICS_PLAN.md): when set, every
  // ship's physics switches to deriving ground/wall/airborne contact purely from the baked
  // collision BVH instead of the analytic corridor/MeshRegion math, on the very next physics step.
  // Mesh physics is now the shipped default (Simulation::meshPhysicsEnabled_ defaults true), so
  // this starts checked to match the GameSession it mirrors; unchecking falls back to analytic.
  bool mMeshPhysicsDebug{true};
  // Live toggle for Simulation::obbWallCollisionEnabled (docs/OBB_SHIP_COLLISION_PLAN.md): when
  // set, mesh-mode wall collision tests the ship's whole hull as an oriented box instead of
  // sweeping one probe point along its centreline. Starts unchecked to match the GameSession it
  // mirrors, and is only meaningful while mesh physics is the active mode.
  bool mObbWallCollisionDebug{false};
  // "Show Physics Ghost": renders mGhostShip -- ship 0 stepped every frame with the same input but
  // forced onto whichever physics mode isn't currently active (via GameSession::stepGhost) -- as a
  // wireframe overlay, so the two methods' predicted positions can be compared live. Resynced to
  // ship 0's current state whenever this flips from off to on, so the comparison always starts from
  // "right now" rather than wherever it last happened to be.
  bool mShowPhysicsGhost{false};
  // Snapshot of ship 0's handling-applied physics, taken once in createGameObjects, so the
  // Physics debug tab's slider ranges (+-20%) and Reset buttons have a stable baseline that
  // isn't itself perturbed by earlier slider edits.
  tox::Physics mInitialShipPhysics;

private:
  mpp::ResourcePtr createShipModel(wp::application::resourcesystem::ResourceManager* resourceMgr,
                                   mpp::ResourceManager* renderResourceMgr);
  TungstenPbrPackage& pbrPackage() const;
  void updatePbrViewport();
  void updateShips(float frameTime);
  void updateChaseCamera(float frameTime);
  void renderHud(mpp::RenderSystem* renderSystem) const;
  void setGeometryKindVisible(tox::GeometryKind kind, bool visible) const;
  void applyTriggersDebugVisibility() const;
  void applyRailsDebugVisibility() const;
  void applyReservationWallsDebugVisibility() const;
  void applyWireframeDebug() const;
  void applyGhostVisibility() const;
  void renderShipPhysicsTab() const;
  void renderPhysicsSlider(char const* label, double& value, double initial) const;

  void createCamera();

  void registerInput() override;

  void createGameObjects(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args) override;

  void destroyGameObjects() override;

  void setupEntityFacades() override;

  void setupEntities() override;

  void updatePreInput(float frameTime) override;

  void updatePreEntities(float frameTime) override;

  void updatePostEntities(float frameTime) override;

  void updateAudio(float frameTime);

  Map* getMap();

  Map const* getMap() const;

  void exit();

protected:
  mpp::RenderPipelinePtr selectRenderPipeline(mpp::RenderSystem* renderSystem) override;

  void updateActions(std::vector<std::string> const& activeStates, float frameTime) override;

  void updatePreRenderers(float frameTime) override;

  void updateCamera(float frameTime) override;

  void setupScene() override;

  void setup(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args) override;

  void suspendImpl(void* args = nullptr) override;

  void resumeImpl(void* args) override;

  void updateImpl(float frameTime) override;

  void renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

  bool _imGuiActive() const override;

  void _renderImGui(float frameTime, void* imGuiCtx, void* imPlotCtx, void* allocFunc, void* freeFunc, void* userData) override;

public:
  StatePlayTungstenMonoxide();

  ~StatePlayTungstenMonoxide();

  std::vector<std::string> getDebuggingText() const override;
};

class StatePlayTungstenMonoxideFactory : public wp::application::StateFactory {
  wp::Logger* mLogger;

public:
  explicit StatePlayTungstenMonoxideFactory(wp::Logger* logger)
      : wp::application::StateFactory("Play"), mLogger(logger) {
  }

  wp::application::State* createState() {
    auto state = new StatePlayTungstenMonoxide();
    state->setLogger(mLogger);
    return state;
  }
};
