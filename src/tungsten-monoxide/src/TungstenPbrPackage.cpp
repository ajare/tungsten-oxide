#include "TungstenPbrPackage.h"

#include <array>
#include <format>
#include <stdexcept>
#include <system_error>

#include <glm/geometric.hpp>

#include <mpp/PbrMaterial.h>
#include <mpp/RenderGraphStream.h>
#include <mpp/RenderSystem.h>
#include <mpp/RenderTexture.h>
#include <mpp/Scene.h>
#include <mpp/ResourceManager.h>
#include <mpp/app/ImageLoader.h>
#include <mpp/app/PackageManifest.h>
#include <mpp/app/ZipArchive.h>
#include <mpp/resource-parsers/PbrPipelineDocumentLoader.h>
#include <mpp/resource-parsers/PbrPipelineRuntime.h>
#include <mpp/resource-parsers/SceneParser.h>

#include <willpower/common/Logger.h>

namespace {
constexpr std::array<std::string_view, 8> RequiredMaterialBindings{
    "Ship.Surface",
    "Track.Asphalt",
    "Track.Rail",
    "Track.Mesh",
    "Track.Shell",
    "Track.Zone",
    "Track.Trigger",
    "Track.Fallback",
};

std::string diagnosticsSummary(mpp::DiagnosticBag const& diagnostics) {
  std::string result;
  for (auto const& diagnostic : diagnostics.getDiagnostics()) {
    if (!result.empty()) {
      result += '\n';
    }
    result += std::format("[{}] {}: {}", diagnostic.code, mpp::diagnosticSeverityName(diagnostic.severity), diagnostic.message);
  }
  return result;
}

void appendErrors(mpp::DiagnosticBag& destination, mpp::DiagnosticBag const& source) {
  destination.append(source);
}
}  // namespace

TungstenPbrPackage::TungstenPbrPackage(std::filesystem::path packagePath, wp::Logger* logger)
    : mPackagePath(std::move(packagePath)), mLogger(logger) {
}

TungstenPbrPackage::~TungstenPbrPackage() {
  shutdown();
}

void TungstenPbrPackage::initialize(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceManager, uint32_t viewportWidth, uint32_t viewportHeight) {
  if (isInitialized()) {
    throw std::logic_error("TungstenMonoxide PBR package is already initialized.");
  }
  if (!renderSystem || !resourceManager) {
    throw std::invalid_argument("TungstenMonoxide PBR package requires render and resource systems.");
  }
  if (viewportWidth == 0 || viewportHeight == 0) {
    throw std::invalid_argument("TungstenMonoxide PBR package requires a non-zero viewport.");
  }

  mRenderSystem = renderSystem;
  mResourceManager = resourceManager;
  if (!resourceManager->getImageLoadFunction()) {
    resourceManager->setImageLoadFunction(mpp::app::loadImageFile);
  }
  mPackagePath = std::filesystem::absolute(mPackagePath).lexically_normal();

  try {
    if (!std::filesystem::is_regular_file(mPackagePath)) {
      throw std::runtime_error(std::format("PBR package does not exist: '{}'", mPackagePath.string()));
    }

    mExtractedDirectory = mpp::app::createUniqueTemporaryDirectory("TungstenMonoxide.Pbr");
    mpp::app::ZipArchive::extract(mPackagePath, mExtractedDirectory);

    auto manifest = mpp::app::readPackageManifest(mExtractedDirectory / "manifest.xml");
    auto pipelineFile = mExtractedDirectory / manifest.pipeline;
    auto sceneFile = mExtractedDirectory / manifest.scene;
    if (!std::filesystem::is_regular_file(pipelineFile) || !std::filesystem::is_regular_file(sceneFile)) {
      throw std::runtime_error("PBR package manifest refers to missing workspace documents.");
    }

    mPipelineDocument = std::make_shared<mpp::PbrPipelineDocument>(
        mpp::resource_parsers::PbrPipelineDocumentLoader::fromFile(pipelineFile.string()));
    mSceneDocument = std::make_unique<mpp::SceneDocument>(mpp::resource_parsers::SceneParser::fromFile(sceneFile.string()));

    mpp::DiagnosticBag documentDiagnostics;
    appendErrors(documentDiagnostics, mPipelineDocument->validate(renderSystem->getCaps(), glm::uvec2(viewportWidth, viewportHeight)));
    appendErrors(documentDiagnostics, mPipelineDocument->validateOutputAntiAliasing(renderSystem->getOptions().antiAliasing, &renderSystem->getCaps()));
    appendErrors(documentDiagnostics, mSceneDocument->validate());
    if (mPipelineDocument->name != PipelineName) {
      documentDiagnostics.error("TMO-PBR-PACKAGE-002",
                                std::format("Pipeline name '{}' must be '{}'.", mPipelineDocument->name, PipelineName),
                                {pipelineFile.string()},
                                "name");
    }
    if (mSceneDocument->environmentBinding != mPipelineDocument->environment.binding) {
      documentDiagnostics.error(
          "TMO-PBR-PACKAGE-001",
          std::format("Scene environment binding '{}' does not match pipeline binding '{}'.", mSceneDocument->environmentBinding, mPipelineDocument->environment.binding),
          {sceneFile.string()},
          "environmentBinding");
    }
    if (documentDiagnostics.hasErrors()) {
      throw std::runtime_error("PBR package document validation failed:\n" + diagnosticsSummary(documentDiagnostics));
    }

    auto graphStream = std::make_shared<mpp::RenderGraphStream>(resourceManager);
    graphStream->setGraph(mPipelineDocument->graph);
    auto declaredGraph = resourceManager->declareResource(std::string(PipelineName) + ".Graph", graphStream);
    if (!declaredGraph.second) {
      throw std::runtime_error("PBR package graph resource is already declared.");
    }
    mGraphResource = declaredGraph.first;
    mGraphResource->load();
    mGraphResource->create();

    mRuntime = std::make_unique<mpp::resource_parsers::PbrPipelineRuntime>(renderSystem, resourceManager);
    if (!mRuntime->rebuild(mPipelineDocument, viewportWidth, viewportHeight)) {
      throw std::runtime_error("PBR package runtime preparation failed:\n" + diagnosticsSummary(mRuntime->getDiagnostics()));
    }

    mMaterialBindings = mRuntime->getMaterialBindings();
    for (auto const binding : RequiredMaterialBindings) {
      auto found = mMaterialBindings.find(std::string(binding));
      if (found == mMaterialBindings.end() || !found->second) {
        throw std::runtime_error(std::format("PBR package is missing required material binding '{}'.", binding));
      }
      if (!dynamic_cast<mpp::PbrMaterial*>(found->second.get())) {
        throw std::runtime_error(std::format("PBR package binding '{}' does not resolve to mpp::PbrMaterial.", binding));
      }
    }

    mpp::RenderPipelineOptions options;
    options.mode = mpp::RenderPipelineMode::XmlGraphPbrForward;
    options.graphTemplate = mGraphResource;
    options.graphImports = mRuntime->getImports();
    options.resourceRoot = mRuntime->getRootResource();
    options.outputs = mPipelineDocument->outputs;
    options.environment = mRuntime->getEnvironment();
    options.bloom.enabled = mPipelineDocument->bloom.enabled;
    options.bloom.blurPasses = mPipelineDocument->bloom.blurPasses;
    mPresentationTarget = mRuntime->getPresentationTarget();
    if (!mPresentationTarget) {
      throw std::runtime_error("PBR package has no presentation target.");
    }

    mLights.clear();
    mLights.reserve(mSceneDocument->lights.size());
    for (auto const& authored : mSceneDocument->lights) {
      mpp::PbrLight light;
      light.type = authored.type == mpp::SceneLightType::Directional ? mpp::PbrLightType::Directional
                                                                     : mpp::PbrLightType::Point;
      light.colour = authored.colour;
      light.intensity = authored.intensity;
      light.position = authored.position;
      light.range = authored.range;
      light.direction = authored.type == mpp::SceneLightType::Directional ? glm::normalize(authored.direction)
                                                                          : authored.direction;
      mLights.push_back(light);
    }

    if (auto direction = mSceneDocument->getShadowLightDirection()) {
      mpp::ShadowOptions shadow;
      shadow.enabled = true;
      shadow.light.direction = glm::normalize(*direction);
      shadow.light.focusPoint = mSceneDocument->camera.target;
      mShadowOptions = shadow;
      options.shadowDomain = ShadowDomainName;
      renderSystem->configureShadowDomain(options.shadowDomain, shadow);
      options.graphImports["shadowDepth"] = renderSystem->getShadowDomainDepthTarget(options.shadowDomain);
    }

    mPipeline = renderSystem->getOrCreateRenderPipeline(PipelineName, options);
    std::map<std::string, mpp::RenderTargetPtr> outputDestinations;
    for (auto const& output : mPipelineDocument->outputs) {
      for (uint32_t image = 0; image < mPipelineDocument->graph->getImageCount(); ++image) {
        auto info = mPipelineDocument->graph->getImageInfo({image, 0});
        if (info.name != output.image || !info.desc.external) {
          continue;
        }
        auto destination = options.graphImports.find(info.importName);
        if (destination != options.graphImports.end()) {
          outputDestinations.emplace(output.name, destination->second);
        }
        break;
      }
    }
    if (outputDestinations.size() != mPipelineDocument->outputs.size()) {
      throw std::runtime_error("PBR package did not resolve every named output destination.");
    }
    mPipeline->prepareOutputs(*mPipelineDocument->graph, outputDestinations);
    mRuntime->accept();
    mViewportWidth = viewportWidth;
    mViewportHeight = viewportHeight;

    if (mLogger) {
      mLogger->info(std::format("Loaded PBR package '{}' with {} material bindings.", mPackagePath.string(), mMaterialBindings.size()));
      for (auto const& diagnostic : mRuntime->getDiagnostics().getDiagnostics()) {
        if (diagnostic.severity == mpp::DiagnosticSeverity::Warning) {
          mLogger->warn(std::format("PBR package [{}]: {}", diagnostic.code, diagnostic.message));
        }
      }
    }
  } catch (...) {
    shutdown();
    throw;
  }
}

void TungstenPbrPackage::shutdown() noexcept {
  mMaterialBindings.clear();
  mLights.clear();

  if (mRenderSystem && mShadowOptions) {
    try {
      auto disabled = *mShadowOptions;
      disabled.enabled = false;
      mRenderSystem->configureShadowDomain(ShadowDomainName, disabled);
    } catch (...) {
    }
  }
  mShadowOptions.reset();

  if (mRenderSystem && mPipeline) {
    try {
      mRenderSystem->removeRenderPipeline(PipelineName);
    } catch (...) {
    }
  }
  mPipeline.reset();
  mRuntime.reset();
  mPresentationTarget.reset();

  if (mResourceManager && mGraphResource) {
    auto graphName = mGraphResource->getName();
    mGraphResource.reset();
    try {
      if (mResourceManager->getResource(graphName, true)) {
        mResourceManager->deleteResourceTree(graphName);
      }
    } catch (...) {
    }
  } else {
    mGraphResource.reset();
  }

  mSceneDocument.reset();
  mPipelineDocument.reset();
  mRenderSystem = nullptr;
  mResourceManager = nullptr;
  mViewportWidth = 0;
  mViewportHeight = 0;

  if (!mExtractedDirectory.empty()) {
    std::error_code error;
    std::filesystem::remove_all(mExtractedDirectory, error);
    if (error && mLogger) {
      mLogger->warn(std::format("Could not remove extracted PBR package directory '{}': {}", mExtractedDirectory.string(), error.message()));
    }
    mExtractedDirectory.clear();
  }
}

bool TungstenPbrPackage::isInitialized() const {
  return mRuntime && mPipeline && mPresentationTarget && mPipelineDocument && mSceneDocument;
}

TungstenPbrPackage::ResolvedMaterial TungstenPbrPackage::resolveMaterial(std::string const& binding) const {
  if (!isInitialized()) {
    throw std::logic_error("TungstenMonoxide PBR package is not initialized.");
  }
  auto found = mMaterialBindings.find(binding);
  if (found == mMaterialBindings.end() || !found->second) {
    throw std::runtime_error(std::format("PBR material binding '{}' is not available.", binding));
  }
  return {found->second, found->second->getName()};
}

void TungstenPbrPackage::applySceneLighting(mpp::Scene* scene) const {
  if (!isInitialized() || !scene) {
    throw std::logic_error("TungstenMonoxide PBR package cannot apply lighting before initialization.");
  }
  scene->setPbrLights(mLights);
}

void TungstenPbrPackage::updateShadowFocus(glm::vec3 const& focusPoint) {
  if (!isInitialized()) {
    throw std::logic_error("TungstenMonoxide PBR package cannot update shadows before initialization.");
  }
  if (!mShadowOptions) return;
  mShadowOptions->light.focusPoint = focusPoint;
  mRenderSystem->configureShadowDomain(ShadowDomainName, *mShadowOptions);
}

void TungstenPbrPackage::resize(uint32_t viewportWidth, uint32_t viewportHeight) {
  if (!isInitialized()) {
    throw std::logic_error("TungstenMonoxide PBR package cannot resize before initialization.");
  }
  if (viewportWidth == 0 || viewportHeight == 0) {
    throw std::invalid_argument("TungstenMonoxide PBR package cannot resize to a zero dimension.");
  }
  if (viewportWidth == mViewportWidth && viewportHeight == mViewportHeight) return;
  try {
    mRuntime->resize(viewportWidth, viewportHeight);
  } catch (std::exception const& error) {
    throw std::runtime_error(std::format(
        "PBR presentation resize to {}x{} failed; the previous {}x{} resources were retained: {}",
        viewportWidth,
        viewportHeight,
        mViewportWidth,
        mViewportHeight,
        error.what()));
  }
  if (mLogger) {
    mLogger->info(std::format("Resized PBR presentation from {}x{} to {}x{}.",
                              mViewportWidth,
                              mViewportHeight,
                              viewportWidth,
                              viewportHeight));
  }
  mViewportWidth = viewportWidth;
  mViewportHeight = viewportHeight;
}

void TungstenPbrPackage::present() {
  if (!isInitialized()) {
    throw std::logic_error("TungstenMonoxide PBR package cannot present before initialization.");
  }
  auto presentationTexture = std::dynamic_pointer_cast<mpp::RenderTexture>(mPresentationTarget);
  if (!presentationTexture) {
    throw std::runtime_error("PBR presentation target is not a render texture.");
  }
  mpp::RenderSystem::TextureDiagnosticOptions options;
  mRenderSystem->renderTextureDiagnostic(presentationTexture.get(), mRenderSystem->getScreenRenderTarget(), options);
}

std::filesystem::path const& TungstenPbrPackage::getPackagePath() const {
  return mPackagePath;
}

std::filesystem::path const& TungstenPbrPackage::getExtractedDirectory() const {
  return mExtractedDirectory;
}

std::shared_ptr<mpp::PbrPipelineDocument> const& TungstenPbrPackage::getPipelineDocument() const {
  return mPipelineDocument;
}

mpp::SceneDocument const& TungstenPbrPackage::getSceneDocument() const {
  if (!mSceneDocument) {
    throw std::logic_error("TungstenMonoxide PBR package scene is not initialized.");
  }
  return *mSceneDocument;
}

mpp::RenderPipelinePtr const& TungstenPbrPackage::getPipeline() const {
  return mPipeline;
}

mpp::RenderTargetPtr const& TungstenPbrPackage::getPresentationTarget() const {
  return mPresentationTarget;
}
