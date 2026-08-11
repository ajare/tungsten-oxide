#include "Viewport.hpp"

#include <algorithm>
#include <atomic>
#include <limits>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#pragma warning(pop)

#include <GL/glew.h>

#include <mpp/ProgrammaticModelStream.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/ResourceWrangler.h>
#include <mpp/Texture.h>
#include <mpp/mesh/MeshSpecification.h>

#include "AssImpImport.hpp"

namespace modeltool {
namespace {

// A fresh unique name every rebuild (toggling visibility, changing size), matching
// ModelResources.cpp's gModelGeneration precedent for the same reason: never looked up by a
// caller-meaningful name, so a generation counter is simplest.
std::atomic<int> gGridGeneration{0};

// Same fixed vertex layout as ModelResources.cpp's fixedMeshSpecification() (so the grid resolves
// to the same core "__mpp_p3d_tris_p3n3t2c4__" program as every other model), but Lines instead of
// Triangles.
mpp::mesh::MeshSpecification gridMeshSpecification() {
  mpp::mesh::MeshSpecification spec(mpp::mesh::Primitive::Type::Lines);
  mpp::mesh::VertexBufferAttributeLayout* layout = spec.createVertexBufferAttributeLayout(false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Position3, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Normal3, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::TexCoord2, mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  spec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  spec.setIndexedVertices(true);
  return spec;
}

struct Aabb {
  glm::vec3 lo{0.0f, 0.0f, 0.0f};
  glm::vec3 hi{0.0f, 0.0f, 0.0f};
  bool any{false};
};

Aabb computeAabb(const ImportedModel& model) {
  Aabb aabb;
  aabb.lo = glm::vec3(std::numeric_limits<float>::max());
  aabb.hi = glm::vec3(-std::numeric_limits<float>::max());
  for (const ImportedMesh& mesh : model.meshes) {
    for (const ImportedVertex& v : mesh.vertices) {
      aabb.any = true;
      aabb.lo.x = std::min(aabb.lo.x, v.px);
      aabb.lo.y = std::min(aabb.lo.y, v.py);
      aabb.lo.z = std::min(aabb.lo.z, v.pz);
      aabb.hi.x = std::max(aabb.hi.x, v.px);
      aabb.hi.y = std::max(aabb.hi.y, v.py);
      aabb.hi.z = std::max(aabb.hi.z, v.pz);
    }
  }
  if (!aabb.any) aabb.lo = aabb.hi = glm::vec3(0.0f);
  return aabb;
}

struct Bounds {
  glm::vec3 center{0.0f, 0.0f, 0.0f};
  float radius{1.0f};
};

Bounds computeBounds(const ImportedModel& model) {
  const Aabb aabb = computeAabb(model);
  if (!aabb.any) return {};
  Bounds bounds;
  bounds.center = (aabb.lo + aabb.hi) * 0.5f;
  // Half the AABB diagonal, not a tight bounding sphere -- cheap to compute and always contains
  // every vertex, which is all auto-framing needs.
  bounds.radius = glm::length(aabb.hi - aabb.lo) * 0.5f;
  return bounds;
}

}  // namespace

Viewport::Viewport(mpp::RenderSystem& renderSystem, mpp::ResourceManager& resourceMgr, mpp::ResourceWrangler& wrangler,
                    MaterialLibrary& materialLibrary)
    : renderSystem_(renderSystem), resourceMgr_(resourceMgr), wrangler_(wrangler), materialLibrary_(materialLibrary) {
  scene_ = std::make_shared<mpp::Scene>(&renderSystem_);
  scene_->load();
  scene_->setClearColour(mpp::Colour(0.12f, 0.12f, 0.14f, 1.0f));
  camera_ = std::make_shared<OrbitCamera>(45.0f, 1.0f);

  // RenderSystem::renderScene() looks its pipeline name up via getRenderPipeline(), which throws
  // if that name was never registered -- it must be created once via getOrCreateRenderPipeline()
  // first (matches ext/massive-poly-pusher/demo-suite/src/ModelScene.cpp's own one-time setup call).
  // Kept (not just created-and-discarded): renderFrame() reads back its own internal SceneTarget
  // texture every frame -- see this header's top comment.
  pipeline_ = renderSystem_.getOrCreateRenderPipeline("ModelToolViewport");

  rebuildGrid();
}

Viewport::~Viewport() {
  destroyGrid();
  if (sceneModel_) scene_->remove3dModel(sceneModel_);
  if (built_.has_value()) releaseBuiltModel(*built_, wrangler_, materialLibrary_);
  if (scene_) scene_->unload();
}

void Viewport::setModel(BuiltModel built) {
  const Bounds bounds = computeBounds(built.source);

  if (built_.has_value()) {
    if (sceneModel_) {
      scene_->remove3dModel(sceneModel_);
      sceneModel_.reset();
    }
    releaseBuiltModel(*built_, wrangler_, materialLibrary_);
    built_.reset();
  }

  built_ = std::move(built);
  previewScale_ = 1.0f;
  undoStack_.clear();
  redoStack_.clear();
  sceneModel_ = scene_->add3dModel(built_->modelResource);
  camera_->frameOnBounds(bounds.center, bounds.radius);
}

glm::vec3 Viewport::sourceExtents() const {
  if (!built_.has_value()) return glm::vec3(0.0f);
  const Aabb aabb = computeAabb(built_->source);
  return aabb.hi - aabb.lo;
}

void Viewport::setPreviewScale(float scale) {
  previewScale_ = scale;
  if (sceneModel_) {
    sceneModel_->resetTransform();
    sceneModel_->scale(glm::vec3(scale));
  }
}

void Viewport::replaceSourceGeometry(ImportedModel newSource) {
  if (sceneModel_) {
    scene_->remove3dModel(sceneModel_);
    sceneModel_.reset();
  }
  // Only the GPU model resource is torn down and rebuilt here -- material references are untouched
  // (see rebuildModelResource()'s comment), so releaseBuiltModel()'s material bookkeeping would be
  // both unnecessary and wrong to run for a bake/undo/redo.
  built_->modelResource->release(&wrangler_);
  built_->modelResource = rebuildModelResource(resourceMgr_, wrangler_, newSource, materialLibrary_.defaultFallbackMaterial()->getName());
  built_->source = std::move(newSource);

  previewScale_ = 1.0f;
  sceneModel_ = scene_->add3dModel(built_->modelResource);

  const Bounds bounds = computeBounds(built_->source);
  camera_->frameOnBounds(bounds.center, bounds.radius);
}

void Viewport::bakeScale() {
  if (!built_.has_value() || previewScale_ == 1.0f) return;
  const float factor = previewScale_;

  undoStack_.push_back(built_->source);
  if (undoStack_.size() > kMaxHistory) undoStack_.erase(undoStack_.begin());
  redoStack_.clear();

  ImportedModel scaled = built_->source;
  for (ImportedMesh& mesh : scaled.meshes) {
    for (ImportedVertex& v : mesh.vertices) {
      v.px *= factor;
      v.py *= factor;
      v.pz *= factor;
    }
  }
  replaceSourceGeometry(std::move(scaled));
}

void Viewport::undo() {
  if (undoStack_.empty() || !built_.has_value()) return;
  redoStack_.push_back(built_->source);
  if (redoStack_.size() > kMaxHistory) redoStack_.erase(redoStack_.begin());
  ImportedModel restored = std::move(undoStack_.back());
  undoStack_.pop_back();
  replaceSourceGeometry(std::move(restored));
}

void Viewport::redo() {
  if (redoStack_.empty() || !built_.has_value()) return;
  undoStack_.push_back(built_->source);
  if (undoStack_.size() > kMaxHistory) undoStack_.erase(undoStack_.begin());
  ImportedModel restored = std::move(redoStack_.back());
  redoStack_.pop_back();
  replaceSourceGeometry(std::move(restored));
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

void Viewport::setGridVisible(bool visible) {
  if (visible == gridVisible_) return;
  gridVisible_ = visible;
  rebuildGrid();
}

void Viewport::setGridSize(double size) {
  if (size == gridSize_) return;
  gridSize_ = size;
  rebuildGrid();
}

void Viewport::destroyGrid() {
  if (gridSceneModel_) {
    scene_->remove3dModel(gridSceneModel_);
    gridSceneModel_.reset();
  }
  if (gridModelResource_) {
    // Matches ModelResources.cpp's releaseBuiltModel(): release only, no deleteResource -- every
    // rebuild gets a fresh generation-numbered name (see gGridGeneration), so nothing is ever
    // looked up by this name again; the same precedent every model Open already relies on.
    gridModelResource_->release(&wrangler_);
    gridModelResource_.reset();
  }
}

void Viewport::rebuildGrid() {
  destroyGrid();
  if (!gridVisible_) return;

  // 1024 units out from the origin in both X and Z (2048 total), lying flat on the Y=0 plane.
  constexpr float kExtent = 1024.0f;
  const float step = static_cast<float>(gridSize_);

  const mpp::mesh::MeshSpecification meshSpec = gridMeshSpecification();
  auto* modelStream = new mpp::ProgrammaticModelStream(&resourceMgr_);
  const std::size_t meshIndex =
      modelStream->createMesh("GridLines", meshSpec, materialLibrary_.defaultFallbackMaterial()->getName(), 16);

  std::vector<ImportedVertex> vertices;
  const auto addLine = [&](float x0, float z0, float x1, float z1) {
    const std::uint32_t i0 = static_cast<std::uint32_t>(vertices.size());
    ImportedVertex a;
    a.px = x0;
    a.py = 0.0f;
    a.pz = z0;
    a.ny = 1.0f;
    a.r = a.g = a.b = 90;
    a.a = 255;
    ImportedVertex b = a;
    b.px = x1;
    b.pz = z1;
    vertices.push_back(a);
    vertices.push_back(b);
    modelStream->addLine(meshIndex, i0, i0 + 1);
  };

  for (float x = -kExtent; x <= kExtent + 0.5f * step; x += step) addLine(x, -kExtent, x, kExtent);
  for (float z = -kExtent; z <= kExtent + 0.5f * step; z += step) addLine(-kExtent, z, kExtent, z);

  const std::vector<std::uint8_t> packed = packVertices(vertices);
  modelStream->addVertexData(meshIndex, std::vector<std::int8_t>(packed.begin(), packed.end()));

  const std::string modelName = "ModelTool.Grid." + std::to_string(gGridGeneration.fetch_add(1));
  gridModelResource_ = resourceMgr_.declareResource(modelName, mpp::ResourceStreamPtr(modelStream)).first;
  gridModelResource_->acquire(&wrangler_);
  gridModelResource_->load();
  gridSceneModel_ = scene_->add3dModel(gridModelResource_);
}

}  // namespace modeltool
