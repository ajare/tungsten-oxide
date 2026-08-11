#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <mpp/PbrPipelineDocument.h>
#include <mpp/PbrLight.h>
#include <mpp/RenderPipeline.h>
#include <mpp/RenderTarget.h>
#include <mpp/Resource.h>
#include <mpp/SceneDocument.h>

#include "Platform.h"

namespace mpp {
class RenderSystem;
class ResourceManager;
class Scene;

namespace resource_parsers {
class PbrPipelineRuntime;
}
}  // namespace mpp

namespace wp {
class Logger;
}

class APPLICATION_API TungstenPbrPackage {
public:
  static constexpr char const* PipelineName = "TungstenMonoxide.Pbr";
  static constexpr char const* ShadowDomainName = "TungstenMonoxide.Pbr.Shadow";

  struct ResolvedMaterial {
    mpp::ResourcePtr resource;
    std::string resourceName;
  };

private:
  std::filesystem::path mPackagePath;
  std::filesystem::path mExtractedDirectory;
  std::shared_ptr<mpp::PbrPipelineDocument> mPipelineDocument;
  std::unique_ptr<mpp::SceneDocument> mSceneDocument;
  std::unique_ptr<mpp::resource_parsers::PbrPipelineRuntime> mRuntime;
  mpp::ResourcePtr mGraphResource;
  mpp::RenderPipelinePtr mPipeline;
  mpp::RenderTargetPtr mPresentationTarget;
  std::map<std::string, mpp::ResourcePtr> mMaterialBindings;
  std::vector<mpp::PbrLight> mLights;
  std::optional<mpp::ShadowOptions> mShadowOptions;
  uint32_t mViewportWidth{0};
  uint32_t mViewportHeight{0};
  mpp::RenderSystem* mRenderSystem{nullptr};
  mpp::ResourceManager* mResourceManager{nullptr};
  wp::Logger* mLogger{nullptr};

public:
  explicit TungstenPbrPackage(std::filesystem::path packagePath, wp::Logger* logger = nullptr);
  ~TungstenPbrPackage();

  TungstenPbrPackage(TungstenPbrPackage const&) = delete;
  TungstenPbrPackage& operator=(TungstenPbrPackage const&) = delete;

  void initialize(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceManager, uint32_t viewportWidth, uint32_t viewportHeight);
  void shutdown() noexcept;

  bool isInitialized() const;
  ResolvedMaterial resolveMaterial(std::string const& binding) const;
  void applySceneLighting(mpp::Scene* scene) const;
  void updateShadowFocus(glm::vec3 const& focusPoint);
  void resize(uint32_t viewportWidth, uint32_t viewportHeight);
  void present();

  std::filesystem::path const& getPackagePath() const;
  std::filesystem::path const& getExtractedDirectory() const;
  std::shared_ptr<mpp::PbrPipelineDocument> const& getPipelineDocument() const;
  mpp::SceneDocument const& getSceneDocument() const;
  mpp::RenderPipelinePtr const& getPipeline() const;
  mpp::RenderTargetPtr const& getPresentationTarget() const;
};
