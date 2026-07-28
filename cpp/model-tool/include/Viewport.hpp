// Viewport.hpp — the right-hand docked panel's live mpp scene, displayed via ImGui::Image() (see
// docs/adr/0001-model-tool.md, D9 -- the first place in this codebase an mpp scene renders into a
// docked ImGui panel rather than to the default framebuffer full-screen). Owns the current
// BuiltModel (ModelResources.hpp) and the OrbitCamera (D8) framed on it.
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
#include <string>

#include <mpp/RenderPipeline.h>
#include <mpp/Scene.h>

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
  Viewport(mpp::RenderSystem& renderSystem, mpp::ResourceManager& resourceMgr, mpp::ResourceWrangler& wrangler);
  ~Viewport();

  Viewport(const Viewport&) = delete;
  Viewport& operator=(const Viewport&) = delete;

  // Releases whatever model is currently loaded (a no-op if none is), then imports and builds a
  // new one, framing the camera on its bounds. Returns an error message on import/build failure
  // (the viewport is left with no model loaded in that case).
  std::optional<std::string> loadModel(const std::string& utf8Path);

  bool hasModel() const { return built_.has_value(); }
  const BuiltModel* builtModel() const { return built_.has_value() ? &*built_ : nullptr; }

  // Renders one frame and returns a GL texture id ready for ImGui::Image() (see this header's top
  // comment on why the *requested* width/height only drive the camera's aspect ratio, not the
  // actual render resolution). Returns 0 if width/height are 0 (panel collapsed this frame).
  unsigned int renderFrame(int width, int height);

  // Mouse-drag-to-orbit / scroll-to-zoom, called from main.cpp's input handling.
  void orbit(float deltaAzimuthDeg, float deltaElevationDeg) { camera_->orbit(deltaAzimuthDeg, deltaElevationDeg); }
  void zoom(float deltaDistance) { camera_->zoom(deltaDistance); }

 private:
  mpp::RenderSystem& renderSystem_;
  mpp::ResourceManager& resourceMgr_;
  mpp::ResourceWrangler& wrangler_;

  mpp::ScenePtr scene_;
  mpp::SceneModel3dPtr sceneModel_;
  std::shared_ptr<OrbitCamera> camera_;
  mpp::RenderPipelinePtr pipeline_;

  std::optional<BuiltModel> built_;
};

}  // namespace modeltool
