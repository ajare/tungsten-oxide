#if defined(__SANITIZE_ADDRESS__)
// Launcher is a GUI application, so send MemCheck reports to a durable file.
// The Visual Studio debugger starts every configuration in $(OutDir), making
// this resolve to the active configuration's output directory.
extern "C" const char* __asan_default_options() {
  return "log_path=Launcher.asan";
}
#endif

#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <stdexcept>

#include "Platform.h"

#include "utils/StringUtils.h"

// GL_UNSIGNED_BYTE/GL_RGBA below used to arrive transitively through an mpp header; that stopped
// once MassivePolyPusher decoupled its public headers from GL, so this now includes GLEW
// directly, matching every other GL-constant user in this codebase.
#include <GL/glew.h>

#if APP_PLATFORM == APP_PLATFORM_WINDOWS
#include <windows.h>

// Ask a hybrid-graphics laptop for its discrete GPU. Both vendors' drivers
// look these symbols up in the export table of the process's own executable,
// so they belong here and not in any DLL the Launcher loads. Without them an
// OpenGL context lands on the integrated adapter: on a Radeon 610M / RTX 5070
// machine the game rendered on the 610M and was GPU-bound at roughly 72 fps
// with a 512x320 render target.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#include <willpower/common/Exceptions.h>
#include <willpower/common/Logger.h>
#include <willpower/common/Timer.h>

#include <willpower/application/ServiceLocator.h>
#include <willpower/application/AudioSystem.h>
#include <willpower/application/ApplicationSettings.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/ProgrammaticTextureStream.h>
#include <mpp/Logger.h>
#include <mpp/BufferRenderer.h>

#if APP_PLATFORM == APP_PLATFORM_WINDOWS
#define SDL_MAIN_HANDLED
#endif
#include <SDL3/SDL.h>
#if APP_PLATFORM == APP_PLATFORM_WINDOWS
#include <SDL3/SDL_main.h>
#endif

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/implot.h"

#include "ExecutableRuntime.hpp"
#include "ProgramOptions.h"
#include "ApplicationDLL.h"
#include "StateManager.h"
#include "ExitApplicationException.h"
#include "willpower/application/resourcesystem/DirectoryResourceLocation.h"
#include "ZipResourceLocation.h"
#include "ImGuiDataProvider.h"

#include "sdl/WindowSDL.h"
#include "sdl/TimerSDL.h"

using namespace std;
using namespace wp;

// Logging
Logger* gLogger = nullptr;
mpp::Logger* gMppLogger = nullptr;

// Platform objects
static ApplicationDLL* gDLL = nullptr;
static StateManager* gStateMgr = nullptr;
static wp::application::AudioSystem* gAudioSystem = nullptr;

static WindowSDL* gWindow = nullptr;
static TimerSDL* gTimer = nullptr;

// Application objects
static application::ApplicationSettings* gAppSettings = nullptr;
static application::resourcesystem::ResourceManager* gResourceMgr = nullptr;

bool gDisplayDebugEnabled = false;

// Rendering objects
static mpp::RenderSystem* gRenderSystem = nullptr;
static mpp::ResourceManager* gRenderSystemResourceMgr = nullptr;
static shared_ptr<ImGuiDataProvider> gImGuiDataProvider;
static mpp::BufferRenderer* gImGuiRenderer = nullptr;

static bool gSdlInitialised = false;
static bool gImGuiInitialised = false;
static bool gRenderCoreResourcesInitialised = false;

void logFatal(const string& message) noexcept {
  tox::runtime::logError(message);
  if (gLogger != nullptr) {
    try {
      gLogger->error(message);
    } catch (...) {
      // The process-wide fallback log above must remain usable even if the HTML logger failed.
    }
  }
}

void initialiseImGui(float contentScale) {
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGui_ImplSDL3_InitForOpenGL(gWindow->getWindow(), gWindow->getContext());

  ImGuiIO& io = ImGui::GetIO();

  // Configure ImGui
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // TODO: Set optional io.ConfigFlags values, e.g. 'io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard' to enable keyboard controls.
  // TODO: Fill optional fields of the io structure later.
  // TODO: Load TTF/OTF fonts if you don't want to use the default font.
  ImFontConfig fontCfg;
  fontCfg.SizePixels = 13.0f * contentScale;

  io.Fonts->AddFontDefault(&fontCfg);

  auto fontRes = gRenderSystemResourceMgr->getResource("__ImGui_Font__", true);
  if (!fontRes) {
    int fontWidth, fontHeight;
    unsigned char* fontData{nullptr};

    io.Fonts->GetTexDataAsRGBA32(&fontData, &fontWidth, &fontHeight);

    auto fontTextureStr = new mpp::ProgrammaticTextureStream(gRenderSystemResourceMgr);

    fontTextureStr->setTarget(mpp::TextureTarget::Texture2D);
    fontTextureStr->setData([fontData, fontWidth, fontHeight](string const&) {
      mpp::TextureData data;

      data.width = fontWidth;
      data.height = fontHeight;
      data.bitsPerPixel = 32;
      data.dataType = GL_UNSIGNED_BYTE;
      data.pixelFormat = GL_RGBA;

      size_t dataSize = (data.width * data.height * data.bitsPerPixel / 8);

      data.data = new uint8_t[dataSize];
      memcpy(data.data, fontData, dataSize);

      return data;
    });

    fontTextureStr->setFiltering(mpp::TextureParams::MinFilter::Linear, mpp::TextureParams::MagFilter::Linear);

    fontRes = gRenderSystemResourceMgr->declareResource("__ImGui_Font__", mpp::ResourceStreamPtr(fontTextureStr)).first;
    fontRes->load();
  }

  io.Fonts->SetTexID((ImTextureID)(intptr_t)fontRes->getId());

  io.DisplaySize.x = (float)gRenderSystem->getWindowWidth();
  io.DisplaySize.y = (float)gRenderSystem->getWindowHeight();

  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes(contentScale);
  gImGuiInitialised = true;
}

//
// Initialise all systems
//
ProgramOptions startup(string const& configFile) {
  // Create loggers
  gLogger = new Logger();
  gLogger->open("LauncherLog.html");

  gMppLogger = new mpp::Logger();
  if (!gMppLogger->initialise("mpp.log", mpp::Logger::Level::Debug)) {
    throw std::runtime_error("Could not create MPP logger!");
  }

  // Read in program options
  ProgramOptions options = parseProgramOptions(configFile);

  logProgramOptions(options, gLogger);

  // Set up application settings
  gAppSettings = new application::ApplicationSettings();
  gAppSettings->VideoWidth = options.screenWidth;
  gAppSettings->VideoHeight = options.screenHeight;
  gAppSettings->Fullscreen = options.fullScreen;

  application::ServiceLocator::provideApplicatonSettings(gAppSettings);

#if defined(__linux__)
  // MPP's pinned GLEW uses GLX, so SDL must not create a Wayland/EGL context.
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11", SDL_HINT_OVERRIDE);
#endif
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw std::runtime_error("Could not initialise SDL subsystem!");
  }
  gSdlInitialised = true;

  // Create timer
  gTimer = new TimerSDL();

  // Create window
  gWindow = new WindowSDL("Window", options);
  gWindow->create();

  // Create render system
  // mpp::enable_static_log(MPP_RESOURCE_LOGFILE, true);

  gRenderSystem = new mpp::RenderSystem(gWindow->getWidth(), gWindow->getHeight(), gMppLogger);
  gRenderSystemResourceMgr = new mpp::ResourceManager(gRenderSystem, gMppLogger);
  gRenderSystem->createCoreResources(gRenderSystemResourceMgr);
  gRenderCoreResourcesInitialised = true;

  // Audio
  gAudioSystem = options.audioEnabled ? new wp::application::AudioSystem(options.audio) : nullptr;

  // Resource manager
  gResourceMgr = new application::resourcesystem::ResourceManager(gRenderSystem, gRenderSystemResourceMgr, gAudioSystem, gLogger);

  // Add resource location factories
  gResourceMgr->addResourceLocationFactory("Directory", [](string const& location, string const& definitionFile) -> application::resourcesystem::ResourceLocation* {
    return new application::resourcesystem::DirectoryResourceLocation(gLogger, location, definitionFile);
  });

  gResourceMgr->addResourceLocationFactory("ZipFile", [](string const& location, string const& definitionFile) -> application::resourcesystem::ResourceLocation* {
    return new ZipResourceLocation(gLogger, location, definitionFile);
  });

  // Add resource locations
  for (auto const& rl : options.resourceLocations) {
    gResourceMgr->addResourceLocation(rl.type, rl.path, rl.definitionFile);
  }

  // ImGui
  initialiseImGui(gWindow->getContentScale());

  vector<mpp::ResourcePtr> imGuiTextures;
  imGuiTextures.push_back(gRenderSystemResourceMgr->getResource("__ImGui_Font__"));

  gImGuiDataProvider = make_shared<ImGuiDataProvider>(imGuiTextures);
  gImGuiRenderer = new mpp::BufferRenderer(gImGuiDataProvider);

  // Load application DLL
  gDLL = new ApplicationDLL();
  gDLL->load(options.dll, options.arguments, gLogger, gResourceMgr);

  // Create state manager and get state factories
  gStateMgr = new StateManager(gResourceMgr, gAudioSystem, gRenderSystem, gRenderSystemResourceMgr);
  gDLL->registerStateFactories(gStateMgr);

  return options;
}

//
// Destroy all systems
//
void shutdown() {
  // Every branch is null/initialisation-safe: startup failures can arrive after any one of these
  // allocations, and error reporting must never be replaced by a second crash during cleanup.
  delete gStateMgr;
  gStateMgr = nullptr;

  delete gImGuiRenderer;
  gImGuiRenderer = nullptr;
  gImGuiDataProvider.reset();
  if (gImGuiInitialised) {
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
    gImGuiInitialised = false;
  }

  delete gResourceMgr;
  gResourceMgr = nullptr;
  delete gAudioSystem;
  gAudioSystem = nullptr;

  if (gRenderSystem != nullptr && gRenderCoreResourcesInitialised) {
    gRenderSystem->destroyCoreResources();
    gRenderCoreResourcesInitialised = false;
  }
  delete gRenderSystem;
  gRenderSystem = nullptr;

  if (gRenderSystemResourceMgr != nullptr) gRenderSystemResourceMgr->dumpResources("final-resources.csv");
  delete gRenderSystemResourceMgr;
  gRenderSystemResourceMgr = nullptr;

  delete gMppLogger;
  gMppLogger = nullptr;
  delete gWindow;
  gWindow = nullptr;
  delete gTimer;
  gTimer = nullptr;

  if (gSdlInitialised) {
    SDL_Quit();
    gSdlInitialised = false;
  }

  delete gAppSettings;
  gAppSettings = nullptr;
  application::ServiceLocator::provideApplicatonSettings(nullptr);
  delete gDLL;
  gDLL = nullptr;
  delete gLogger;
  gLogger = nullptr;
}

//
// Helper to set up debug text to display
//

void setupDebugPanel() {
  string fpsColour;
  float fps = gTimer->getFPS();
  if (fps < 30) {
    fpsColour = "[#FF0000FF]";
  } else if (fps < 55) {
    fpsColour = "[#FFFF00FF]";
  } else {
    fpsColour = "[#00FF00FF]";
  }

  string fpsDisplay = std::format("FPS: {}{}", fpsColour, (int)fps);
  gRenderSystem->setDebugPreMessages({fpsDisplay});

  gRenderSystem->setDebugPostMessages(gStateMgr->getDebuggingText());

  gRenderSystem->showDebugPanel(gDisplayDebugEnabled,
                                mpp::RenderSystem::TimeUnit::Milliseconds,
                                mpp::RenderSystem::SizeUnit::Megabytes);
}

void updateImGui(float frameTime) {
  if (gStateMgr->imGuiActive()) {
    gWindow->showCursor(true);

    ImGui_ImplSDL3_NewFrame();

    ImGuiIO& io = ImGui::GetIO();

    io.DeltaTime = frameTime;

    ImGui::NewFrame();

    // Pass the contexts across the DLL boundary
    auto imGuiCtx = ImGui::GetCurrentContext();
    auto imPlotCtx = ImPlot::GetCurrentContext();

    ImGuiMemAllocFunc imGuiAllocFunc;
    ImGuiMemFreeFunc imGuiFreeFunc;
    void* imGuiUserData;

    ImGui::GetAllocatorFunctions(&imGuiAllocFunc, &imGuiFreeFunc, &imGuiUserData);

    // Willpower's ABI intentionally carries allocator callbacks as opaque pointers. Convert via
    // uintptr_t because standard C++ does not allow a direct function-pointer-to-void* conversion.
    auto* allocOpaque = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(imGuiAllocFunc));
    auto* freeOpaque = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(imGuiFreeFunc));
    gStateMgr->renderImGui(frameTime, imGuiCtx, imPlotCtx, allocOpaque, freeOpaque, imGuiUserData);

    ImGui::EndFrame();
    ImGui::Render();

    gImGuiDataProvider->setDrawData(ImGui::GetDrawData());
  } else {
    gWindow->showCursor(false);
  }
}

//
// Entry point
//
int launcherMain(int argc, char** argv) {
  string configFile = "Game.yaml";
  if (argc > 1) {
    configFile = string(argv[1]);
  }

  int exitCode = 0;
  uint64_t numFramesProcessed{0};
  double totalTime{0};
  int64_t totalTimeNs{0};
  try {
    auto options = startup(configFile);

    // Main loop
    float accum = 0.0f;
    const float updateFreq = 1.0f / 60.0f;

    mpp::RenderInfo renderInfo;

    gStateMgr->enterInitialState(options.gameResource);
    gTimer->reset();

    while (true) {
      // Get frame time
      float frameTime = gTimer->getDeltaTime();
      gTimer->addFrameToCounter(frameTime);

      accum += frameTime;

      // Process window messages
      gWindow->processEvents(gStateMgr);

      // Update current state
      while (accum >= updateFreq) {
        accum -= updateFreq;

        updateImGui(updateFreq);

        auto startTime = static_cast<double>(SDL_GetTicksNS()) / 1000000000.0;

        wp::Timer timerNs;
        auto startTimeNs = timerNs.elapsedNanoseconds();

        gStateMgr->update(updateFreq);

        if (gAudioSystem) {
          gAudioSystem->update();
        }

        auto endTime = static_cast<double>(SDL_GetTicksNS()) / 1000000000.0;
        auto endTimeNs = timerNs.elapsedNanoseconds();

        numFramesProcessed++;

        totalTime += (endTime - startTime);
        totalTimeNs += (endTimeNs - startTimeNs);
      }

      // Render
      setupDebugPanel();

      gRenderSystem->startStatsCollection();

      gStateMgr->render(gRenderSystem, gRenderSystemResourceMgr);

      auto ri = gRenderSystem->finishStatsCollection();

      if (gStateMgr->imGuiActive()) {
        gImGuiRenderer->render(gRenderSystem);
      }

      // Flip to screen
      gWindow->show();
    }
  } catch (ExitApplicationException& e) {
    if (numFramesProcessed > 0 && gLogger != nullptr) {
      gLogger->info(format("Avg update time ms: {}", totalTime / numFramesProcessed * 1000.0));
      gLogger->info(format("Avg update time ms: {}", totalTimeNs / numFramesProcessed / 1000000.0));
    }

    exitCode = e.getExitCode();
    if (exitCode == 0 && gLogger != nullptr)
      gLogger->info(e.getMessage());
    else
      logFatal(e.getMessage());
  } catch (application::resourcesystem::ResourceException& e) {
    const auto resource = e.getResource();
    logFatal(resource != nullptr ? "Error in resource '" + resource->getQualifiedName() + "': " + e.what()
                                 : std::string("Resource error: ") + e.what());
    exitCode = 1;
  } catch (application::resourcesystem::ResourceSystemException& e) {
    logFatal(std::string("Resource system error: ") + e.what());
    exitCode = 1;
  } catch (Exception& e) {
    logFatal(e.what());
    exitCode = 1;
  } catch (exception& e) {
    logFatal(e.what());
    exitCode = 1;
  } catch (...) {
    logFatal("Unknown fatal error");
    exitCode = 1;
  }

  shutdown();
  return exitCode;
}

#if APP_PLATFORM == APP_PLATFORM_WINDOWS
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  SDL_SetMainReady();
  return tox::runtime::guardedMain([] { return launcherMain(__argc, __argv); });
}
#else
int main(int argc, char** argv) {
  return tox::runtime::guardedMain([&] { return launcherMain(argc, argv); });
}
#endif
