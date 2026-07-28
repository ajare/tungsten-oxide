// cpp/model-tool/main.cpp — model_tool: imports OBJ/FBX/USD/glTF via AssImp, previews the result
// in a live mpp viewport, and saves it as .mppmodel. See docs/adr/0001-model-tool.md for the
// design this implements.
//
// GLEW must be the first GL-touching include in this translation unit (before SDL.h/windows.h),
// matching the rest of this app's GLEW-as-GL-loader choice (ADR D2, include/imgui/imconfig.h).
#include <glew/glew.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"  // ImGui::DockBuilder* -- used once at startup to build the fixed layout
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>

#include <mpp/Logger.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/ResourceWrangler.h>

#include "FileDialog.hpp"
#include "ModelResourceExport.hpp"
#include "MppSave.hpp"
#include "Viewport.hpp"

namespace {

// Keys every viewport resource against, mirroring cpp/tungsten-monoxide's TmResourceWrangler
// (StatePlayTungstenMonoxide.h) -- a plain named ResourceWrangler with no behaviour of its own.
class AppWrangler : public mpp::ResourceWrangler {
 public:
  AppWrangler() : ResourceWrangler("ModelTool") {}
};

}  // namespace

int main(int argc, char** argv) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  // No SDL_GL_CONTEXT_PROFILE_MASK (compatibility profile) and GL 3.2, matching cpp/launcher's own
  // proven-working combination for hosting mpp::RenderSystem -- unlike cpp/editor's explicit 3.0
  // *core* profile request, which never has to satisfy mpp's own (non-core-only) GL usage.
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  const SDL_WindowFlags windowFlags = static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Window* window = SDL_CreateWindow("model_tool", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, windowFlags);
  if (window == nullptr) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_GLContext glContext = SDL_GL_CreateContext(window);
  if (glContext == nullptr) {
    std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_GL_MakeCurrent(window, glContext);
  SDL_GL_SetSwapInterval(1);  // vsync

  mpp::Logger mppLogger;
  if (!mppLogger.initialise("model_tool_mpp.log", mpp::Logger::Level::Debug)) {
    std::fprintf(stderr, "mpp::Logger::initialise failed\n");
  }

  int drawableWidth = 0, drawableHeight = 0;
  SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);

  // RenderSystem's constructor calls glewInit() internally (see mpp/src/RenderSystem.cpp) -- no
  // separate glewInit() call needed here, matching every other host of mpp::RenderSystem in this
  // codebase (cpp/launcher, ext/massivepolypusher/demo-suite).
  auto* renderSystem = new mpp::RenderSystem(static_cast<std::size_t>(drawableWidth), static_cast<std::size_t>(drawableHeight), &mppLogger);
  auto* resourceMgr = new mpp::ResourceManager(renderSystem, &mppLogger);
  renderSystem->createCoreResources(resourceMgr);

  AppWrangler wrangler;
  auto* viewport = new modeltool::Viewport(*renderSystem, *resourceMgr, wrangler);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // No imgui.ini: the fixed layout built once below (menu bar / toolbar / left panel / viewport,
  // matching cpp/editor's own docked-shell convention) is meant to be the same every launch.
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  io.Fonts->AddFontDefault();

  ImGui_ImplSDL2_InitForOpenGL(window, glContext);
  ImGui_ImplOpenGL3_Init("#version 130");

  // Status bar message, mirrors cpp/editor/main.cpp's own showStatus() convention: the most recent
  // message replaces whatever's showing and is displayed for 3 seconds.
  std::string statusMessage;
  std::chrono::steady_clock::time_point statusExpiresAt{};
  auto showStatus = [&](std::string message) {
    statusMessage = std::move(message);
    statusExpiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  };

  // Optional CLI arg: model_tool.exe <path> auto-imports at startup, mainly useful for headless/
  // scripted debugging without driving the Import... dialog by hand.
  if (argc > 1) {
    if (const auto error = viewport->loadModel(argv[1])) {
      showStatus("Import failed: " + *error);
    } else {
      showStatus(std::string("Loaded ") + argv[1]);
    }
  }

  bool running = true;
  bool dockLayoutBuilt = false;

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) running = false;
      if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
        running = false;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        // One "Import..." item, combined filter across all four formats (ADR 0001 D11) -- AssImp
        // dispatches by content/extension regardless of which filter entry the user picked.
        if (ImGui::MenuItem("Import...")) {
          const modeltool::FileDialogResult picked = modeltool::showOpenFileDialog(
              L"Import Model",
              {{L"All Supported Models (*.obj;*.fbx;*.usd;*.usda;*.usdc;*.usdz;*.gltf;*.glb)",
                L"*.obj;*.fbx;*.usd;*.usda;*.usdc;*.usdz;*.gltf;*.glb"},
               {L"OBJ (*.obj)", L"*.obj"},
               {L"FBX (*.fbx)", L"*.fbx"},
               {L"USD (*.usd;*.usda;*.usdc;*.usdz)", L"*.usd;*.usda;*.usdc;*.usdz"},
               {L"glTF (*.gltf;*.glb)", L"*.gltf;*.glb"}});
          if (picked.ok) {
            const std::string path = modeltool::pathToUtf8(picked.path);
            if (const auto error = viewport->loadModel(path)) {
              showStatus("Import failed: " + *error);
            } else {
              showStatus("Loaded " + path);
            }
          }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save As .mppmodel...", nullptr, false, viewport->hasModel())) {
          const modeltool::FileDialogResult picked =
              modeltool::showSaveFileDialog(L"Save mppmodel", {{L"MassivePolyPusher Model (*.mppmodel)", L"*.mppmodel"}}, L"model.mppmodel", L"mppmodel");
          if (picked.ok) {
            std::string error;
            const std::string path = modeltool::pathToUtf8(picked.path);
            if (modeltool::saveModelAsMppModel(*viewport->builtModel(), path, &error)) {
              // Companion Resources.xml-shaped fragment declaring this model's materials (see
              // ModelResourceExport.hpp), written beside the .mppmodel with the same stem --
              // mirrors cpp/editor/main.cpp's own Export MppModel flow (buildTrackResourceXml).
              const std::filesystem::path xmlPath = std::filesystem::path(picked.path).replace_extension(L"xml");
              const std::string xml =
                  modeltool::buildModelMaterialsXml(viewport->builtModel()->source, modeltool::pathToUtf8(picked.path.stem()));
              std::ofstream xmlOut(xmlPath, std::ios::binary);
              if (xmlOut) {
                xmlOut.write(xml.data(), static_cast<std::streamsize>(xml.size()));
                showStatus("Wrote " + path + " and " + modeltool::pathToUtf8(xmlPath));
              } else {
                showStatus("Wrote " + path + " (failed to write companion XML)");
              }
            } else {
              showStatus("Save failed: " + error);
            }
          }
        }
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    // Toolbar: a fixed strip pinned directly under the menu bar, mirrors cpp/editor/main.cpp's own
    // toolbar construction (not part of the dockspace, not movable/resizable).
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x, mainViewport->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::Begin("##Toolbar", nullptr,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("model_tool");
    ImGui::SameLine();
    ImGui::TextDisabled("| drag the viewport to orbit, scroll to zoom");
    const float toolbarHeight = ImGui::GetWindowSize().y;
    ImGui::End();
    ImGui::PopStyleVar();

    // Status bar strip, reserved space out of the dockspace host below -- same fixed/non-dockable
    // construction as the toolbar above.
    const float statusBarHeight = ImGui::GetTextLineHeightWithSpacing() + 16.0f;
    const bool statusVisible = !statusMessage.empty() && std::chrono::steady_clock::now() < statusExpiresAt;

    // Dockspace host: fills the remaining space between the toolbar and the status bar. Built once
    // via DockBuilder on the first frame only (io.IniFilename is null, so there's no saved layout
    // to conflict with) -- left panel + right viewport, mirroring cpp/editor's own left-panel-plus-
    // views arrangement.
    ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x, mainViewport->WorkPos.y + toolbarHeight));
    ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, mainViewport->WorkSize.y - toolbarHeight - statusBarHeight));
    ImGui::SetNextWindowViewport(mainViewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##DockSpaceHost", nullptr,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    const ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    if (!dockLayoutBuilt) {
      dockLayoutBuilt = true;
      ImGui::DockBuilderRemoveNode(dockspaceId);
      ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(dockspaceId, mainViewport->WorkSize);

      ImGuiID leftId = 0, rightId = 0;
      ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.26f, &leftId, &rightId);
      ImGui::DockBuilderDockWindow("Panels", leftId);
      ImGui::DockBuilderDockWindow("Viewport", rightId);

      ImGui::DockBuilderFinish(dockspaceId);
    }
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();

    // Left panel: flat mesh list + flat material list, no node/scene-graph tree (ADR 0001 D10 --
    // .mppmodel itself has no node concept, and node transforms are already baked into vertex data
    // at import time).
    ImGui::Begin("Panels");
    if (const modeltool::BuiltModel* built = viewport->builtModel()) {
      if (ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const modeltool::ImportedMesh& mesh : built->source.meshes)
          ImGui::BulletText("%s (%zu tris)", mesh.name.c_str(), mesh.indices.size() / 3);
      }
      if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const modeltool::ImportedMaterial& material : built->source.materials) {
          if (material.diffuseTexturePath.has_value()) {
            ImGui::BulletText("%s", material.name.c_str());
            ImGui::TextDisabled("    %s", material.diffuseTexturePath->c_str());
          } else if (material.skippedEmbeddedTexture) {
            ImGui::BulletText("%s: embedded texture skipped (default white)", material.name.c_str());
          } else {
            ImGui::BulletText("%s: default white", material.name.c_str());
          }
        }
      }
    } else {
      ImGui::TextDisabled("No model loaded -- File > Import...");
    }
    ImGui::End();

    // Viewport panel: renders the current model into an mpp::RenderTexture sized to the panel's
    // available content region, then displays it via ImGui::Image() (ADR 0001 D9). V is flipped
    // (uv0={0,1}, uv1={1,0}) since a GL texture's row 0 is its bottom, not its top.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport");
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const unsigned int textureId = viewport->renderFrame(static_cast<int>(avail.x), static_cast<int>(avail.y));
    if (textureId != 0) {
      ImGui::Image(static_cast<ImTextureID>(static_cast<std::intptr_t>(textureId)), avail, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
      if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
          const ImVec2 delta = ImGui::GetIO().MouseDelta;
          viewport->orbit(-delta.x * 0.3f, delta.y * 0.3f);
        }
        if (io.MouseWheel != 0.0f) viewport->zoom(io.MouseWheel);
      }
    } else {
      ImGui::TextDisabled("(viewport collapsed)");
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // Status bar.
    ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x, mainViewport->WorkPos.y + mainViewport->WorkSize.y - statusBarHeight));
    ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, statusBarHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::Begin("##StatusBar", nullptr,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(statusVisible ? statusMessage.c_str() : "Ready");
    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::Render();
    SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
    glViewport(0, 0, drawableWidth, drawableHeight);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  // Explicit teardown order (viewport/its resources, then core resources, then the systems that
  // own them) while the GL context is still current -- mirrors
  // ext/massivepolypusher/demo-suite's own shutdown() ordering.
  delete viewport;
  renderSystem->destroyCoreResources();
  delete resourceMgr;
  delete renderSystem;

  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
