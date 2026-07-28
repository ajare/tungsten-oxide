// Viewport.hpp — the right-hand docked panel's live mpp scene, displayed via ImGui::Image() (see
// docs/adr/0001-model-tool.md, D9 -- the first place in this codebase an mpp scene renders into a
// docked ImGui panel rather than to the default framebuffer full-screen). Owns the current
// BuiltModel (ModelResources.hpp) and the OrbitCamera (D8) framed on it.
//
// Import/material-resolution orchestration lives in main.cpp, not here: setModel() only accepts an
// ALREADY-BUILT model (materials fully resolved -- see MaterialLibrary.hpp's Replace/Ignore
// conflict handling, which may span several frames via a modal before a model is ready). Viewport's
// only responsibility is swapping it into the Scene and releasing whatever was there before.
//
// Does NOT push its own RenderTarget around renderScene() (an earlier version of this file did,
// and it silently rendered into nothing visible): mpp::RenderPipeline's default RenderPass
// constructs and owns its own internal "SceneTarget" texture (mpp/src/RenderPass.cpp), sized to
// the *main window* at the point the pipeline was first created, and its bindRenderTarget()
// unconditionally binds that texture -- overriding whatever RenderTarget was pushed beforehand.
// This reads back that same internal SceneTarget via RenderPipeline::getOutputRenderTarget()
// instead of trying to redirect the pipeline into a custom per-panel-sized target (no public API
// for that was found). Consequence: the rendered image is always at the main window's resolution,
// not the docked panel's -- ImGui just displays/stretches it into whatever panel space is
// available. Not pixel-perfect, but genuinely visible, which the pushed-target approach wasn't.
#pragma once

#include <memory>
#include <optional>

#include <mpp/RenderPipeline.h>
#include <mpp/Scene.h>

#include "MaterialLibrary.hpp"
#include "ModelResources.hpp"
#include "OrbitCamera.hpp"

namespace mpp {
class RenderSystem;
class ResourceManager;
class ResourceWrangler;
}  // namespace mpp

namespace modeltool {

class Viewport {
 public:
  Viewport(mpp::RenderSystem& renderSystem, mpp::ResourceManager& resourceMgr, mpp::ResourceWrangler& wrangler,
            MaterialLibrary& materialLibrary);
  ~Viewport();

  Viewport(const Viewport&) = delete;
  Viewport& operator=(const Viewport&) = delete;

  // Releases whatever model is currently loaded (a no-op if none is, via MaterialLibrary so any
  // ModelOwned material it referenced gets its refcount decremented), then adopts `built` and
  // frames the camera on its bounds. `built`'s own material references must already be resolved
  // (and, for any name shared with the outgoing model, acquired BEFORE this call -- see
  // main.cpp's finalizeModelBuild()) so a shared name's refcount never transiently drops to zero.
  void setModel(BuiltModel built);

  bool hasModel() const { return built_.has_value(); }
  const BuiltModel* builtModel() const { return built_.has_value() ? &*built_ : nullptr; }

  // Renders one frame and returns a GL texture id ready for ImGui::Image() (see this header's top
  // comment on why the *requested* width/height only drive the camera's aspect ratio, not the
  // actual render resolution). Returns 0 if width/height are 0 (panel collapsed this frame).
  unsigned int renderFrame(int width, int height);

  // Mouse-drag-to-orbit / scroll-to-zoom, called from main.cpp's input handling.
  void orbit(float deltaAzimuthDeg, float deltaElevationDeg) { camera_->orbit(deltaAzimuthDeg, deltaElevationDeg); }
  void zoom(float deltaDistance) { camera_->zoom(deltaDistance); }

  // Optional XZ reference grid, mirroring cpp/editor's top-down grid (TopDownView.hpp's
  // showGrid_/gridSize_ defaults: visible, 32 units) -- independent of whatever model is loaded
  // (or whether one is at all), rebuilt into its own Lines-primitive Model3d resource whenever
  // visibility or size changes. Extends 1024 units out from the origin along both X and Z (2048
  // total), lying flat on the Y=0 plane.
  bool gridVisible() const { return gridVisible_; }
  void setGridVisible(bool visible);
  double gridSize() const { return gridSize_; }
  void setGridSize(double size);

 private:
  void rebuildGrid();
  void destroyGrid();

  mpp::RenderSystem& renderSystem_;
  mpp::ResourceManager& resourceMgr_;
  mpp::ResourceWrangler& wrangler_;
  MaterialLibrary& materialLibrary_;

  mpp::ScenePtr scene_;
  mpp::SceneModel3dPtr sceneModel_;
  std::shared_ptr<OrbitCamera> camera_;
  mpp::RenderPipelinePtr pipeline_;

  std::optional<BuiltModel> built_;

  bool gridVisible_{true};
  double gridSize_{32.0};
  mpp::ResourcePtr gridModelResource_;
  mpp::SceneModel3dPtr gridSceneModel_;
};

}  // namespace modeltool
