#if defined(__SANITIZE_ADDRESS__)
// Launcher is a GUI application, so send MemCheck reports to a durable file.
// The Visual Studio debugger starts every configuration in $(OutDir), making
// this resolve to the active configuration's output directory.
extern "C" const char* __asan_default_options() {
  return "log_path=Launcher.asan";
}
#endif

#include "Platform.h"

#if APP_PLATFORM == APP_PLATFORM_WINDOWS

#include <format>
#include <iostream>

#include "utils/StringUtils.h"

// GL_UNSIGNED_BYTE/GL_RGBA below used to arrive transitively through an mpp header; that stopped
// once MassivePolyPusher decoupled its public headers from GL, so this now includes GLEW
// directly, matching every other GL-constant user in this codebase.
#include <GL/glew.h>

#include <windows.h>

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

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/implot.h"

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
    throw exception("Could not create MPP logger!");
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

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw exception("Could not initialise SDL subsystem!");
  }

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
  // States own scenes, package runtimes, and resources backed by the systems below.
  // Unwind them while every dependency and the application DLL are still alive.
  delete gStateMgr;
  gStateMgr = nullptr;

  // ImGui
  delete gImGuiRenderer;
  gImGuiRenderer = nullptr;

  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  ImPlot::DestroyContext();

  // Destroy resource manager
  delete gResourceMgr;
  gResourceMgr = nullptr;

  // Destroy audio
  delete gAudioSystem;
  gAudioSystem = nullptr;

  // Destroy render system
  gRenderSystem->destroyCoreResources();
  delete gRenderSystem;
  gRenderSystem = nullptr;

  gRenderSystemResourceMgr->dumpResources("final-resources.csv");
  delete gRenderSystemResourceMgr;
  gRenderSystemResourceMgr = nullptr;

  delete gMppLogger;
  gMppLogger = nullptr;

  // Destroy window
  delete gWindow;
  gWindow = nullptr;

  // Destroy timer
  delete gTimer;
  gTimer = nullptr;

  // Shut down SDL
  SDL_Quit();

  // Destroy application
  delete gAppSettings;
  gAppSettings = nullptr;
  application::ServiceLocator::provideApplicatonSettings(nullptr);

  // Destroy application DLL
  delete gDLL;
  gDLL = nullptr;

  // Destroy logger
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

    gStateMgr->renderImGui(frameTime, imGuiCtx, imPlotCtx, imGuiAllocFunc, imGuiFreeFunc, imGuiUserData);

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
int main(int argc, char** argv) {
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
    auto upt = totalTime / numFramesProcessed;
    gLogger->info(format("Avg update time ms: {}", upt * 1000.0));

    auto uptNs = totalTimeNs / numFramesProcessed;
    gLogger->info(format("Avg update time ms: {}", uptNs / 1000000.0));

    gLogger->info(e.getMessage());
    exitCode = e.getExitCode();
  } catch (application::resourcesystem::ResourceException& e) {
    auto res = e.getResource();
    gLogger->error("Error in resource: " + res->getQualifiedName());
    gLogger->error(e.what());
    exitCode = 1;

#ifdef _DEBUG
    char const* msg = e.what();

    size_t reqLength = ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), 0, 0);
    wstring ret(reqLength, L'\0');

    ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), &ret[0], (int)ret.length());
    OutputDebugString(ret.c_str());
#endif
  } catch (application::resourcesystem::ResourceSystemException& e) {
    gLogger->error(e.what());
    exitCode = 1;

#ifdef _DEBUG
    char const* msg = e.what();

    size_t reqLength = ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), 0, 0);
    wstring ret(reqLength, L'\0');

    ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), &ret[0], (int)ret.length());
    OutputDebugString(ret.c_str());
#endif
  } catch (Exception& e) {
    gLogger->error(e.what());
    exitCode = 1;

#ifdef _DEBUG
    char const* msg = e.what();

    size_t reqLength = ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), 0, 0);
    wstring ret(reqLength, L'\0');

    ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), &ret[0], (int)ret.length());
    OutputDebugString(ret.c_str());
#endif
  } catch (exception& e) {
    gLogger->error(e.what());
    exitCode = 1;

#ifdef _DEBUG
    char const* msg = e.what();

    size_t reqLength = ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), 0, 0);
    wstring ret(reqLength, L'\0');

    ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), &ret[0], (int)ret.length());
    OutputDebugString(ret.c_str());
#endif
  }

  shutdown();
  return exitCode;
}

#else
#error "Launcher is supported only on Windows because it loads Windows application DLLs."
#endif