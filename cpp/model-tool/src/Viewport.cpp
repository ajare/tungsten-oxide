#include "Viewport.hpp"

#include <algorithm>
#include <limits>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#pragma warning(pop)

#include <glew/glew.h>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/ResourceWrangler.h>
#include <mpp/Texture.h>

#include "AssImpImport.hpp"

namespace modeltool {
namespace {

struct Bounds {
  glm::vec3 center{0.0f, 0.0f, 0.0f};
  float radius{1.0f};
};

Bounds computeBounds(const ImportedModel& model) {
  glm::vec3 lo(std::numeric_limits<float>::max());
  glm::vec3 hi(-std::numeric_limits<float>::max());
  bool any = false;
  for (const ImportedMesh& mesh : model.meshes) {
    for (const ImportedVertex& v : mesh.vertices) {
      any = true;
      lo.x = std::min(lo.x, v.px);
      lo.y = std::min(lo.y, v.py);
      lo.z = std::min(lo.z, v.pz);
      hi.x = std::max(hi.x, v.px);
      hi.y = std::max(hi.y, v.py);
      hi.z = std::max(hi.z, v.pz);
    }
  }
  if (!any) return {};
  Bounds bounds;
  bounds.center = (lo + hi) * 0.5f;
  // Half the AABB diagonal, not a tight bounding sphere -- cheap to compute and always contains
  // every vertex, which is all auto-framing needs.
  bounds.radius = glm::length(hi - lo) * 0.5f;
  return bounds;
}

}  // namespace

Viewport::Viewport(mpp::RenderSystem& renderSystem, mpp::ResourceManager& resourceMgr, mpp::ResourceWrangler& wrangler)
    : renderSystem_(renderSystem), resourceMgr_(resourceMgr), wrangler_(wrangler) {
  scene_ = std::make_shared<mpp::Scene>(&renderSystem_);
  scene_->load();
  scene_->setClearColour(mpp::Colour(0.12f, 0.12f, 0.14f, 1.0f));
  camera_ = std::make_shared<OrbitCamera>(45.0f, 1.0f);

  // RenderSystem::renderScene() looks its pipeline name up via getRenderPipeline(), which throws
  // if that name was never registered -- it must be created once via getOrCreateRenderPipeline()
  // first (matches ext/massivepolypusher/demo-suite/src/ModelScene.cpp's own one-time setup call).
  // Kept (not just created-and-discarded): renderFrame() reads back its own internal SceneTarget
  // texture every frame -- see this header's top comment.
  pipeline_ = renderSystem_.getOrCreateRenderPipeline("ModelToolViewport");
}

Viewport::~Viewport() {
  if (sceneModel_) scene_->remove3dModel(sceneModel_);
  if (built_.has_value()) releaseBuiltModel(*built_, wrangler_);
  if (scene_) scene_->unload();
}

std::optional<std::string> Viewport::loadModel(const std::string& utf8Path) {
  std::string error;
  std::optional<ImportedModel> imported = importModel(utf8Path, &error);
  if (!imported.has_value()) return error;

  const Bounds bounds = computeBounds(*imported);

  if (built_.has_value()) {
    if (sceneModel_) {
      scene_->remove3dModel(sceneModel_);
      sceneModel_.reset();
    }
    releaseBuiltModel(*built_, wrangler_);
    built_.reset();
  }

  built_ = buildModel(resourceMgr_, wrangler_, std::move(*imported));
  sceneModel_ = scene_->add3dModel(built_->modelResource);
  camera_->frameOnBounds(bounds.center, bounds.radius);
  return std::nullopt;
}

unsigned int Viewport::renderFrame(int width, int height) {
  if (width <= 0 || height <= 0) return 0;

  // The pipeline's own internal SceneTarget (see this header's top comment) is what actually gets
  // rendered into -- its size is fixed at RenderPass construction (RenderPass::RenderPass(),
  // mpp/src/RenderPass.cpp), so both the scene viewport and the camera's aspect ratio are set from
  // *that* target's own dimensions, not the requested panel size: rendering at the panel's smaller
  // size into a window-sized target would leave most of the texture as stale/cleared content from
  // a previous frame, and using the panel's aspect ratio for the camera while actually rendering
  // at the target's aspect ratio would visibly stretch/squash the image.
  const mpp::RenderTargetPtr outputTarget = pipeline_->getOutputRenderTarget();
  const auto targetWidth = outputTarget->getWidth();
  const auto targetHeight = outputTarget->getHeight();
  camera_->setAspectRatio(static_cast<float>(targetWidth) / static_cast<float>(targetHeight));
  scene_->setViewport(0, 0, targetWidth, targetHeight);

  renderSystem_.setAmbientColour(mpp::Colour(0.35f, 0.35f, 0.35f));
  renderSystem_.setLightCount(1);
  renderSystem_.setLight1Position(camera_->getPosition());
  renderSystem_.setLight1Colour(mpp::Colour::White);
  // RenderPipeline::render() (invoked here) clears the screen and binds its own SceneTarget
  // itself (RenderPass::bindRenderTarget()) -- no manual pushRenderTarget()/clearScreen() needed
  // or indeed effective, since bindRenderTarget() would override a manually pushed target anyway.
  renderSystem_.renderScene(scene_, mpp::CameraPtr(camera_), glm::vec2(0.0f, 0.0f), "ModelToolViewport");

  // RenderTexture is-a Texture as well as a RenderTarget (see RenderTexture.h); Texture's own GL
  // texture id has no public getter, so bind attachment 0 to a texture unit and read back which id
  // that bound (matching what Texture::bind() itself does internally) -- the only way to recover a
  // raw GLuint for ImGui::Image() without modifying the vendored mpp library.
  auto* texture = dynamic_cast<mpp::Texture*>(outputTarget.get());
  texture->bind(0, 0);
  GLint boundId = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundId);
  return static_cast<unsigned int>(boundId);
}

}  // namespace modeltool
