// cpp/model-tool/main.cpp — model_tool: imports OBJ/FBX/USD/glTF via AssImp, or an existing
// .mppmodel file (see MppModelImport.hpp), previews the result in a live mpp viewport, and saves
// it back out as .mppmodel. See docs/adr/0001-model-tool.md for the design this implements.
//
// GLEW must be the first GL-touching include in this translation unit (before SDL.h/windows.h),
// matching the rest of this app's GLEW-as-GL-loader choice (ADR D2, include/imgui/imconfig.h).
#include <glew/glew.h>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec3.hpp>
#pragma warning(pop)

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

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
#include "MaterialLibrary.hpp"
#include "MaterialXmlImport.hpp"
#include "ModelResourceExport.hpp"
#include "MppModelImport.hpp"
#include "MppSave.hpp"
#include "Viewport.hpp"
#include "fontawesome/IconsFontAwesome5.h"

namespace {

// Locates cpp/model-tool/resources/<filename> by walking up from the current working directory --
// mirrors cpp/editor/main.cpp's own findEditorResourceFile() for the same reason: model_tool's
// build output directory is nested several levels under the repo root, and the FontAwesome font
// this finds is never copied to the output directory (unlike the mpp/AssImp runtime DLLs).
std::filesystem::path findModelToolResourceFile(const std::string& filename) {
  std::filesystem::path dir = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    std::error_code ec;
    const std::filesystem::path candidate = dir / "cpp" / "model-tool" / "resources" / filename;
    if (std::filesystem::exists(candidate, ec)) return candidate;
    if (!dir.has_parent_path() || dir.parent_path() == dir) break;
    dir = dir.parent_path();
  }
  return {};
}

// Plain suffix compare on the UTF-8 path string, not std::filesystem::path::extension() --
// avoids that ACP-narrowing footgun entirely for a check this simple (matches
// AssImpImport.cpp's own utf8FileStem() reasoning).
bool hasExtension(const std::string& utf8Path, const char* extensionLowercase) {
  const std::size_t extLen = std::strlen(extensionLowercase);
  if (utf8Path.size() < extLen) return false;
  std::string suffix = utf8Path.substr(utf8Path.size() - extLen);
  for (char& c : suffix) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return suffix == extensionLowercase;
}

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
  // materialLibrary is constructed before viewport (which holds a reference to it) and destroyed
  // after it (see the explicit teardown order near the bottom of main()).
  auto* materialLibrary = new modeltool::MaterialLibrary(*resourceMgr, wrangler);
  auto* viewport = new modeltool::Viewport(*renderSystem, *resourceMgr, wrangler, *materialLibrary);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // No imgui.ini: the fixed layout built once below (menu bar / toolbar / left panel / viewport,
  // matching cpp/editor's own docked-shell convention) is meant to be the same every launch.
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();

  // FontAwesome icons (material Unload button): merged into the default font atlas rather than
  // loaded standalone, so ICON_FA_* glyphs sit inline in ordinary button/text labels at the
  // default font's baseline/line height (the standard ImFontConfig::MergeMode icon-font recipe --
  // mirrors cpp/editor/main.cpp's identical setup, see cpp/editor/include/fontawesome/
  // README-VENDORED.md). Missing the font file just falls back to the default font with no icons.
  constexpr float kBaseFontSize = 13.0f;  // ImGui's own default font size.
  // AddFontDefault() needs an explicit SizePixels here -- see cpp/editor/main.cpp's identical
  // comment: this vendored ImGui's font-atlas code otherwise sets ImFontFlags_ImplicitRefSize,
  // which trips an assert when MergeMode is used with an explicit SizePixels below.
  ImFontConfig defaultFontConfig;
  defaultFontConfig.SizePixels = kBaseFontSize;
  io.Fonts->AddFontDefault(&defaultFontConfig);
  const std::filesystem::path iconFontPath = findModelToolResourceFile(FONT_ICON_FILE_NAME_FAS);
  if (!iconFontPath.empty()) {
    constexpr float kIconFontSize = kBaseFontSize * 2.0f / 3.0f;  // FontAwesome's own sizing recipe
    static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    iconConfig.GlyphMinAdvanceX = kIconFontSize;
    io.Fonts->AddFontFromFileTTF(modeltool::pathToUtf8(iconFontPath).c_str(), kIconFontSize, &iconConfig, iconRanges);
  }
  // No manual io.Fonts->Build() here: this vendored ImGui's OpenGL3/SDL2 backends use the newer
  // texture-management path (ImGuiBackendFlags_RendererHasTextures, set by ImGui_ImplOpenGL3_Init
  // below), which builds/uploads the atlas lazily on first use.

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

  // One row per name collision, surfaced either by doImportMaterialsXml() (a plain UserImported
  // conflict) or by beginModelImport() below (an Embedded-material conflict for a model still
  // being loaded -- modelMaterialIndex/embeddedStream are set in that case). Resolved via the
  // "Material Name Conflicts" modal further down the frame loop; `choice` is driven directly by
  // the modal's per-row RadioButtons: 0 = Replace, 1 = Ignore.
  struct PendingMaterialConflict {
    std::string qualifiedName;
    std::optional<std::string> texturePath;
    std::string sourceFile;
    int choice{0};
    std::optional<std::size_t> modelMaterialIndex;  // set => belongs to pendingModelImport below
    mpp::ResourceStreamPtr embeddedStream;           // set only for a .mppmodel Embedded conflict
  };
  std::vector<PendingMaterialConflict> pendingConflicts;
  bool openConflictDialog = false;

  // A model (AssImp- or .mppmodel-sourced) whose Embedded materials collided with something
  // already loaded -- its build is deferred until every row above tagged with a
  // modelMaterialIndex is resolved via the modal's Apply button (see finalizeModelBuild() below).
  // At most one import is ever pending at a time (the modal blocks starting another).
  struct PendingModelImport {
    modeltool::ImportedModel imported;
    std::string sourceFileForDisplay;
    std::vector<std::optional<modeltool::MaterialReference>> materialRefs;  // parallel to imported.materials
    std::vector<std::string> unresolvedMaterialNames;                      // for the finish-up status message
  };
  std::optional<PendingModelImport> pendingModelImport;

  // Builds the live Model resource from a fully-resolved PendingModelImport (every materialRefs[i]
  // populated except DefaultFallback slots) and swaps it into the viewport. Wrapped in try/catch:
  // this is where deserialized-material Resource::load() actually runs (GL texture upload, program
  // compilation), and an exception thrown from inside mpp/vendored code here would otherwise
  // propagate all the way out of the frame loop uncaught -- a silent process termination with no
  // status message at all, rather than a diagnosable one.
  auto finalizeModelBuild = [&](PendingModelImport pending) {
    const std::string sourceFileForDisplay = pending.sourceFileForDisplay;
    const std::size_t unresolvedCount = pending.unresolvedMaterialNames.size();
    try {
      const std::string defaultFallbackName = materialLibrary->defaultFallbackMaterial()->getName();
      modeltool::BuiltModel built =
          modeltool::buildModel(*resourceMgr, wrangler, std::move(pending.imported), std::move(pending.materialRefs), defaultFallbackName);
      viewport->setModel(std::move(built));
    } catch (const std::exception& error) {
      showStatus("Failed to finish loading " + sourceFileForDisplay + ": " + error.what());
      return;
    }

    std::string message = "Loaded " + sourceFileForDisplay;
    if (unresolvedCount > 0)
      message += " -- " + std::to_string(unresolvedCount) + " material(s) not found, using default white (see Materials panel)";
    showStatus(message);
  };

  // Called after any MaterialLibrary mutation that could resolve a previously-unresolved material
  // name (importing Materials XML, or Replace/Ignore in the conflict modal below) -- if the
  // currently-loaded model has any DefaultFallback-origin material whose (bare, never-resolved)
  // name is now loaded, rebuilds the model so its mesh(es) actually use the real material instead
  // of the shared default-white fallback. A no-op if nothing currently loaded needed upgrading.
  //
  // acquireExistingReference() is called for EVERY still-resolved entry (not just the newly
  // upgraded ones), not just the fallback ones being upgraded: Viewport::setModel() releases the
  // OUTGOING model's own material references as part of the swap, so the incoming (rebuilt) model
  // needs its own fresh references acquired first -- the same "acquire new before releasing old"
  // ordering beginModelImport()/Viewport::setModel() already rely on (see their own comments) --
  // otherwise a name shared between the old and new model instance would transiently (or
  // permanently, since nothing else would still be holding it) drop to a zero refcount.
  auto refreshCurrentModelMaterials = [&]() {
    const modeltool::BuiltModel* current = viewport->builtModel();
    if (current == nullptr) return;

    modeltool::ImportedModel updated = current->source;
    bool anyUpgraded = false;
    for (modeltool::ImportedMaterial& material : updated.materials) {
      if (material.origin == modeltool::MaterialOrigin::DefaultFallback && materialLibrary->contains(material.name)) {
        material.diffuseTexturePath = materialLibrary->materials().at(material.name).texturePath;
        material.origin = modeltool::MaterialOrigin::ExternalReference;
        anyUpgraded = true;
      }
    }
    if (!anyUpgraded) return;

    std::vector<std::optional<modeltool::MaterialReference>> materialRefs(updated.materials.size());
    for (std::size_t i = 0; i < updated.materials.size(); ++i) {
      if (updated.materials[i].origin != modeltool::MaterialOrigin::DefaultFallback)
        materialRefs[i] = materialLibrary->acquireExistingReference(updated.materials[i].name);
    }

    const std::string sourceFileForDisplay = updated.sourcePath;
    try {
      const std::string defaultFallbackName = materialLibrary->defaultFallbackMaterial()->getName();
      modeltool::BuiltModel built = modeltool::buildModel(*resourceMgr, wrangler, std::move(updated), std::move(materialRefs), defaultFallbackName);
      viewport->setModel(std::move(built));
      showStatus("Updated " + sourceFileForDisplay + " to use newly loaded material(s)");
    } catch (const std::exception& error) {
      showStatus("Failed to refresh " + sourceFileForDisplay + ": " + error.what());
    }
  };

  // Shared by doOpen() (AssImp path) and MppModelImport's caller below: resolves every material in
  // `imported` against MaterialLibrary and either finishes the model immediately (no conflicts) or
  // defers it behind the conflict modal. `embeddedStreams` is parallel to imported.materials and
  // only populated (non-null entries) for .mppmodel-sourced Embedded materials -- empty for AssImp,
  // whose Embedded materials are always built fresh from a texture file path instead.
  auto beginModelImport = [&](modeltool::ImportedModel imported, const std::string& sourceFileForDisplay,
                               std::vector<mpp::ResourceStreamPtr> embeddedStreams, std::vector<std::string> unresolvedMaterialNames) {
    PendingModelImport pending;
    pending.sourceFileForDisplay = sourceFileForDisplay;
    pending.unresolvedMaterialNames = std::move(unresolvedMaterialNames);
    pending.materialRefs.resize(imported.materials.size());

    // Wrapped in try/catch: declareModelOwned()/declareModelOwnedFromStream() call Resource::load()
    // on a (for .mppmodel) deserialized material -- vendored mpp/ResourceStreamSerializer code this
    // app doesn't control, which can throw for a corrupt or unusual embedded material. Without this,
    // that exception would propagate out of the whole frame loop uncaught and silently terminate the
    // process rather than reporting a status message. Any materials already declared/acquired before
    // the throw are released again here so nothing leaks.
    bool anyConflict = false;
    try {
      for (std::size_t i = 0; i < imported.materials.size(); ++i) {
        const modeltool::ImportedMaterial& material = imported.materials[i];
        switch (material.origin) {
          case modeltool::MaterialOrigin::DefaultFallback:
            break;  // no reference needed -- buildModel() resolves this by the shared fallback name
          case modeltool::MaterialOrigin::ExternalReference:
            // "check that the named materials are loaded and use them" -- a lookup against
            // something already loaded can't collide with anything, so this is never deferred.
            pending.materialRefs[i] = materialLibrary->acquireExistingReference(material.name);
            break;
          case modeltool::MaterialOrigin::Embedded:
            if (materialLibrary->contains(material.name)) {
              PendingMaterialConflict conflict;
              conflict.qualifiedName = material.name;
              conflict.texturePath = material.diffuseTexturePath;
              conflict.sourceFile = sourceFileForDisplay;
              conflict.modelMaterialIndex = i;
              if (i < embeddedStreams.size()) conflict.embeddedStream = embeddedStreams[i];
              pendingConflicts.push_back(std::move(conflict));
              anyConflict = true;
            } else {
              pending.materialRefs[i] =
                  (i < embeddedStreams.size() && embeddedStreams[i])
                      ? materialLibrary->declareModelOwnedFromStream(material.name, embeddedStreams[i], material.diffuseTexturePath,
                                                                      sourceFileForDisplay)
                      : materialLibrary->declareModelOwned(material.name, material.diffuseTexturePath, sourceFileForDisplay);
            }
            break;
        }
      }
    } catch (const std::exception& error) {
      for (std::optional<modeltool::MaterialReference>& ref : pending.materialRefs)
        if (ref.has_value()) materialLibrary->releaseModelReference(*ref);
      pendingConflicts.clear();
      showStatus("Failed to load " + sourceFileForDisplay + ": " + error.what());
      return;
    }

    pending.imported = std::move(imported);

    if (anyConflict) {
      pendingModelImport = std::move(pending);
      openConflictDialog = true;
      showStatus(std::to_string(pendingConflicts.size()) + " material name conflict(s) -- resolve them to finish loading " +
                 sourceFileForDisplay);
    } else {
      finalizeModelBuild(std::move(pending));
    }
  };

  // Dispatches by extension: .mppmodel goes through MppModelImport.hpp (mpp::ModelSerializer
  // directly), everything else through AssImp. Shared by doOpen() (after the file dialog) and the
  // CLI-arg auto-import below.
  auto openPath = [&](const std::string& path) {
    if (hasExtension(path, ".mppmodel")) {
      std::string error;
      std::optional<modeltool::MppModelImportResult> result = modeltool::importMppModel(path, *resourceMgr, *materialLibrary, &error);
      if (!result.has_value()) {
        showStatus("Open failed: " + error);
        return;
      }
      beginModelImport(std::move(result->model), path, std::move(result->embeddedMaterialStreams), std::move(result->unresolvedMaterialNames));
    } else {
      std::string error;
      std::optional<modeltool::ImportedModel> imported = modeltool::importModel(path, &error);
      if (!imported.has_value()) {
        showStatus("Open failed: " + error);
        return;
      }
      beginModelImport(std::move(*imported), path, {}, {});
    }
  };

  // Shared by the File menu's "Open..."/"Save As .mppmodel..." items and their Ctrl+O/Ctrl+S
  // keyboard shortcuts below, so both paths run identical logic.
  auto doOpen = [&]() {
    // *.mppmodel is folded into "All Supported Models" alongside the AssImp formats (doOpen()/
    // openPath() dispatch by extension regardless of which filter entry the user picked, same as
    // the AssImp formats already do among themselves), plus its own separate filter entry for
    // picking it specifically.
    const modeltool::FileDialogResult picked = modeltool::showOpenFileDialog(
        L"Open Model",
        {{L"All Supported Models (*.obj;*.fbx;*.usd;*.usda;*.usdc;*.usdz;*.gltf;*.glb;*.mppmodel)",
          L"*.obj;*.fbx;*.usd;*.usda;*.usdc;*.usdz;*.gltf;*.glb;*.mppmodel"},
         {L"OBJ (*.obj)", L"*.obj"},
         {L"FBX (*.fbx)", L"*.fbx"},
         {L"USD (*.usd;*.usda;*.usdc;*.usdz)", L"*.usd;*.usda;*.usdc;*.usdz"},
         {L"glTF (*.gltf;*.glb)", L"*.gltf;*.glb"},
         {L"MassivePolyPusher Model (*.mppmodel)", L"*.mppmodel"}});
    if (!picked.ok) return;
    openPath(modeltool::pathToUtf8(picked.path));
  };

  auto doSave = [&]() {
    if (!viewport->hasModel()) return;
    const modeltool::FileDialogResult picked =
        modeltool::showSaveFileDialog(L"Save mppmodel", {{L"MassivePolyPusher Model (*.mppmodel)", L"*.mppmodel"}}, L"model.mppmodel", L"mppmodel");
    if (!picked.ok) return;
    std::string error;
    const std::string path = modeltool::pathToUtf8(picked.path);
    try {
      if (modeltool::saveModelAsMppModel(*viewport->builtModel(), *materialLibrary, path, &error)) {
        // Companion Resources.xml-shaped fragment declaring this model's materials (see
        // ModelResourceExport.hpp) -- the ONLY place they're described, since the .mppmodel itself
        // now references materials by name only, mirroring cpp/editor/main.cpp's own Export
        // MppModel flow (buildTrackResourceXml) exactly, materials-wise.
        const std::filesystem::path xmlPath = std::filesystem::path(picked.path).replace_extension(L"xml");
        const std::string xml =
            modeltool::buildModelMaterialsXml(viewport->builtModel()->source, materialLibrary->defaultFallbackMaterial()->getName());
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
    } catch (const std::exception& saveError) {
      showStatus(std::string("Save failed: ") + saveError.what());
    }
  };

  // Multi-select Resources.xml import: non-conflicting materials are declared immediately;
  // materials whose qualified name is already loaded are queued into pendingConflicts instead,
  // deferring the replace/ignore decision to the user via the modal opened below.
  auto doImportMaterialsXml = [&]() {
    const modeltool::FileDialogMultiResult picked =
        modeltool::showOpenMultipleFilesDialog(L"Import Materials XML", {{L"Resources XML (*.xml)", L"*.xml"}});
    if (!picked.ok) return;

    int importedCount = 0;
    int fileErrorCount = 0;

    for (const std::filesystem::path& path : picked.paths) {
      const std::string utf8Path = modeltool::pathToUtf8(path);
      std::string error;
      const auto file = modeltool::importMaterialXml(utf8Path, &error);
      if (!file.has_value()) {
        ++fileErrorCount;
        continue;
      }
      for (const modeltool::ImportedMaterialXmlEntry& entry : file->materials) {
        if (materialLibrary->contains(entry.qualifiedName)) {
          pendingConflicts.push_back({entry.qualifiedName, entry.texturePath, utf8Path, 0, std::nullopt, {}});
        } else {
          materialLibrary->addUserImported(entry.qualifiedName, entry.texturePath, utf8Path);
          ++importedCount;
        }
      }
    }

    if (!pendingConflicts.empty()) openConflictDialog = true;

    std::string message = "Imported " + std::to_string(importedCount) + " material(s)";
    if (fileErrorCount > 0) message += ", " + std::to_string(fileErrorCount) + " file(s) failed to parse";
    if (!pendingConflicts.empty()) message += ", " + std::to_string(pendingConflicts.size()) + " name conflict(s) pending";
    showStatus(message);

    // Called last so its own, more specific status message (if anything actually got upgraded)
    // wins over the generic import summary above.
    refreshCurrentModelMaterials();
  };

  // Optional CLI arg: model_tool.exe <path> auto-imports at startup, mainly useful for headless/
  // scripted debugging without driving the Open... dialog by hand.
  if (argc > 1) openPath(argv[1]);

  bool running = true;
  bool dockLayoutBuilt = false;

  // Scale panel state: which axis' extent targetSize is measured against, applied as a live
  // (visual-only) preview via Viewport::setPreviewScale() and committed to vertex data via
  // Viewport::bakeScale().
  int scaleAxisIndex = 0;  // 0 = X, 1 = Y, 2 = Z
  float scaleTargetSize = 1.0f;

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

    // Ctrl+O/Ctrl+S/Ctrl+Z/Ctrl+Y, global since there's no text-input widget yet that would need to
    // steal these keys (mirrors cpp/editor/main.cpp's own !io.WantTextInput-guarded shortcut block).
    if (!io.WantTextInput && io.KeyCtrl) {
      if (ImGui::IsKeyPressed(ImGuiKey_O)) doOpen();
      if (ImGui::IsKeyPressed(ImGuiKey_S)) doSave();
      if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (viewport->canUndo()) {
          viewport->undo();
          showStatus("Undo");
        }
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
        if (viewport->canRedo()) {
          viewport->redo();
          showStatus("Redo");
        }
      }
    }
    // G toggles the viewport's reference grid, unmodified (mirrors cpp/editor's own G shortcut for
    // its top-down grid -- TopDownView.hpp/main.cpp).
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_G)) viewport->setGridVisible(!viewport->gridVisible());

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        // One "Open..." item, combined filter across all four AssImp formats (ADR 0001 D11) plus
        // a separate *.mppmodel filter entry -- doOpen()/openPath() dispatch by extension.
        if (ImGui::MenuItem("Open...", "Ctrl+O")) doOpen();
        ImGui::Separator();
        if (ImGui::MenuItem("Save As .mppmodel...", "Ctrl+S", false, viewport->hasModel())) doSave();
        ImGui::Separator();
        if (ImGui::MenuItem("Import Materials XML...")) doImportMaterialsXml();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) running = false;
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Edit")) {
        // Undo/redo covers geometry-mutating edits only (currently just Bake Scale) -- see
        // Viewport::undo()/redo()'s comment.
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, viewport->canUndo())) {
          viewport->undo();
          showStatus("Undo");
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, viewport->canRedo())) {
          viewport->redo();
          showStatus("Redo");
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("View")) {
        // Mirrors cpp/editor's own "Show Grid" / "Grid Size" View menu entries (main.cpp) and
        // TopDownView.hpp's defaults (visible, 32 units) -- see Viewport.hpp's gridVisible()/
        // gridSize() comment for why this grid extends 1024 units out from the origin instead of
        // following the camera/model bounds like the editor's top-down one does.
        bool gridVisible = viewport->gridVisible();
        if (ImGui::MenuItem("Show Grid", "G", &gridVisible)) viewport->setGridVisible(gridVisible);
        if (ImGui::BeginMenu("Grid Size", gridVisible)) {
          const int gridSizeOptions[] = {8, 16, 32, 64};
          for (int option : gridSizeOptions) {
            const bool selected = static_cast<double>(option) == viewport->gridSize();
            if (ImGui::MenuItem(std::to_string(option).c_str(), nullptr, selected)) viewport->setGridSize(static_cast<double>(option));
          }
          ImGui::EndMenu();
        }
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    // "Material Name Conflicts" modal: one row per PendingMaterialConflict queued above (either a
    // plain XML-import UserImported conflict, or an Embedded-material conflict for a model whose
    // build is deferred in pendingModelImport), each with its own Replace/Ignore choice -- applied
    // all at once when the user clicks Apply, matching "replace or ignore, for each one" rather
    // than an all-or-nothing prompt. OpenPopup() only fires on the frame the flag flips true;
    // BeginPopupModal itself then owns the popup's open/closed state across frames.
    if (openConflictDialog) {
      ImGui::OpenPopup("Material Name Conflicts");
      openConflictDialog = false;
    }
    if (ImGui::BeginPopupModal("Material Name Conflicts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("These material names are already loaded. Choose Replace or Ignore for each:");
      ImGui::Separator();
      for (PendingMaterialConflict& conflict : pendingConflicts) {
        ImGui::PushID(conflict.qualifiedName.c_str());
        ImGui::TextUnformatted(conflict.qualifiedName.c_str());
        ImGui::SameLine(300.0f);
        ImGui::RadioButton("Replace", &conflict.choice, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Ignore", &conflict.choice, 1);
        ImGui::PopID();
      }
      ImGui::Separator();
      if (ImGui::Button("Apply")) {
        // Wrapped in try/catch: Replace declares/loads a material (possibly a deserialized
        // .mppmodel stream, vendored code this app doesn't control and which can throw) -- without
        // this, that exception would propagate out of the frame loop uncaught and silently
        // terminate the process instead of reporting a status message.
        try {
          int replacedCount = 0;
          for (const PendingMaterialConflict& conflict : pendingConflicts) {
            if (conflict.modelMaterialIndex.has_value()) {
              // Belongs to pendingModelImport: Replace declares a fresh entry (from the
              // deserialized .mppmodel stream when there is one, else built from the texture path
              // -- AssImp conflicts never carry a stream); Ignore acquires a reference to the one
              // already loaded. Either way the model's own materialRefs slot gets the resulting
              // token.
              modeltool::MaterialReference ref =
                  conflict.choice == 0
                      ? (conflict.embeddedStream
                             ? materialLibrary->declareModelOwnedFromStream(conflict.qualifiedName, conflict.embeddedStream,
                                                                            conflict.texturePath, conflict.sourceFile)
                             : materialLibrary->declareModelOwned(conflict.qualifiedName, conflict.texturePath, conflict.sourceFile))
                      : *materialLibrary->acquireExistingReference(conflict.qualifiedName);
              pendingModelImport->materialRefs[*conflict.modelMaterialIndex] = ref;
              if (conflict.choice == 0) ++replacedCount;
            } else if (conflict.choice == 0) {
              materialLibrary->addUserImported(conflict.qualifiedName, conflict.texturePath, conflict.sourceFile);
              ++replacedCount;
            }
          }
          pendingConflicts.clear();
          if (pendingModelImport.has_value()) {
            PendingModelImport pending = std::move(*pendingModelImport);
            pendingModelImport.reset();
            finalizeModelBuild(std::move(pending));
          } else {
            showStatus("Replaced " + std::to_string(replacedCount) + " material(s)");
            // Only when this batch wasn't itself finishing a model load -- that model was just
            // built fresh above with everything it needed already resolved.
            refreshCurrentModelMaterials();
          }
        } catch (const std::exception& error) {
          pendingConflicts.clear();
          pendingModelImport.reset();
          showStatus(std::string("Failed to apply conflict resolution: ") + error.what());
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        pendingConflicts.clear();
        if (pendingModelImport.has_value()) {
          // The model can never finish loading without every material resolved -- release
          // whatever references this import already acquired (ExternalReference lookups, and any
          // Embedded ones that didn't conflict) so nothing leaks, then drop the import entirely.
          for (std::optional<modeltool::MaterialReference>& ref : pendingModelImport->materialRefs)
            if (ref.has_value()) materialLibrary->releaseModelReference(*ref);
          pendingModelImport.reset();
          showStatus("Import cancelled");
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const float toolbarHeight = 0.0f;

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

    // Left panel: flat mesh list + one unified material list, no node/scene-graph tree (ADR 0001
    // D10 -- .mppmodel itself has no node concept, and node transforms are already baked into
    // vertex data at import time).
    ImGui::Begin("Panels");
    modeltool::BuiltModel* built = viewport->mutableBuiltModel();
    if (built != nullptr) {
      if (ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
        int meshIndex = 0;
        for (modeltool::ImportedMesh& mesh : built->source.meshes) {
          ImGui::PushID(meshIndex++);
          const modeltool::ImportedMaterial& material = built->source.materials[static_cast<std::size_t>(mesh.materialIndex)];
          ImGui::BulletText("%s (%zu tris)", mesh.name.c_str(), mesh.indices.size() / 3);
          // DefaultFallback's own `name` is the original, never-resolved bare material name (see
          // MppModelImport.cpp/AssImpImport.cpp) -- flagged the same way the Materials section
          // below flags it, rather than implying the mesh actually has that material bound.
          if (material.origin == modeltool::MaterialOrigin::DefaultFallback) {
            ImGui::TextDisabled("    %s: NOT FOUND -- using default white", material.name.c_str());
          } else {
            ImGui::TextDisabled("    %s", material.name.c_str());
          }
          // Collidable/decorative flag (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 4.1/4.3): saved
          // into the exported .mppmodel name (CollidableFlag.hpp), consumed by the game host when
          // this model is placed as a drivable mesh object.
          ImGui::Indent();
          ImGui::Checkbox("Collidable", &mesh.collidable);
          ImGui::Unindent();
          ImGui::PopID();
        }
      }
    } else {
      ImGui::TextDisabled("No model loaded -- File > Open...");
    }

    if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
      // Unified by name: MaterialLibrary and the current model's own material list share the same
      // qualified-name space, so a name appearing in both is exactly one entity, not two separate
      // ones to track and display independently. A DefaultFallback entry can never appear in
      // MaterialLibrary by construction (it means "not currently loaded"), so it's listed
      // separately below -- loading a material under one of these names (e.g. via "Import
      // Materials XML...") automatically picks the model up, see refreshCurrentModelMaterials().
      std::set<std::string> unresolvedNames;
      if (built != nullptr) {
        for (const modeltool::ImportedMaterial& material : built->source.materials)
          if (material.origin == modeltool::MaterialOrigin::DefaultFallback) unresolvedNames.insert(material.name);
      }

      if (materialLibrary->materials().empty() && unresolvedNames.empty()) {
        ImGui::TextDisabled("None -- File > Import Materials XML..., or open a model");
      } else {
        // Collected rather than removed mid-loop: MaterialLibrary::remove() mutates the very map
        // being iterated here.
        std::vector<std::string> toRemove;
        for (const auto& [name, material] : materialLibrary->materials()) {
          ImGui::PushID(name.c_str());
          ImGui::Bullet();
          ImGui::SameLine();
          ImGui::TextUnformatted(name.c_str());
          // Disabled while a currently-loaded model still references this material (modelRefCount
          // > 0), regardless of provenance: the underlying mpp Resource is only ever acquire()'d
          // once, by the single AppWrangler shared across this whole app (MaterialLibrary and
          // Viewport both acquire against the same wrangler instance) -- Unload's release() would
          // drop its real refcount to zero and delete it immediately, out from under whatever's
          // still rendering with it, no matter how many app-level "logical" users modelRefCount
          // still counts. Provenance only decides AUTOMATIC cleanup on model replace (ModelOwned
          // only, see MaterialLibrary::releaseModelReference()) -- a UserImported material with no
          // model currently using it (modelRefCount == 0) is still always manually unloadable.
          const bool inUse = material.modelRefCount > 0;
          if (inUse) {
            ImGui::SameLine();
            ImGui::TextDisabled("(in use)");
          }
          ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24.0f);
          ImGui::BeginDisabled(inUse);
          if (ImGui::Button(ICON_FA_TRASH)) toRemove.push_back(name);
          ImGui::EndDisabled();
          if (ImGui::IsItemHovered()) ImGui::SetTooltip(inUse ? "In use by the current model" : "Unload");
          if (material.texturePath.has_value()) {
            ImGui::TextDisabled("    %s", material.texturePath->c_str());
          } else {
            ImGui::TextDisabled("    (no texture)");
          }
          const std::wstring sourceFileName = std::filesystem::path(modeltool::utf8ToWide(material.sourceFile)).filename();
          ImGui::TextDisabled("    from %s", modeltool::wideToUtf8(sourceFileName).c_str());
          ImGui::PopID();
        }
        for (const std::string& name : toRemove) materialLibrary->remove(name);

        for (const std::string& name : unresolvedNames) {
          ImGui::PushID(name.c_str());
          ImGui::BulletText("%s: NOT FOUND -- using default white", name.c_str());
          ImGui::PopID();
        }
      }
    }

    if (ImGui::CollapsingHeader("Scale", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (built == nullptr) {
        ImGui::TextDisabled("No model loaded");
      } else {
        // sourceExtents() is always the un-baked model's own size -- scaling is proportional to
        // that, not to whatever preview scale (if any) is already dialed in, so repeatedly
        // adjusting the axis/target before baking doesn't compound.
        const glm::vec3 extents = viewport->sourceExtents();
        const float axisExtents[3] = {extents.x, extents.y, extents.z};
        ImGui::Text("Current size: %.3f x %.3f x %.3f", extents.x, extents.y, extents.z);

        const char* axisLabels[] = {"X", "Y", "Z"};
        ImGui::Combo("Axis", &scaleAxisIndex, axisLabels, 3);
        ImGui::InputFloat("Target Size", &scaleTargetSize);
        if (scaleTargetSize < 0.0f) scaleTargetSize = 0.0f;

        const float currentExtent = axisExtents[scaleAxisIndex];
        const bool canApply = currentExtent > 1e-6f && scaleTargetSize > 0.0f;
        ImGui::BeginDisabled(!canApply);
        if (ImGui::Button("Apply Scale (Preview)")) viewport->setPreviewScale(scaleTargetSize / currentExtent);
        ImGui::EndDisabled();

        ImGui::Text("Preview scale: x%.4f", viewport->previewScale());
        ImGui::SameLine();
        // Bake multiplies every vertex position by the current preview scale, rebuilds the GPU
        // model from the scaled data, and resets the preview scale back to 1.0 -- disabled when
        // there's nothing to bake (no scale applied yet).
        ImGui::BeginDisabled(viewport->previewScale() == 1.0f);
        if (ImGui::Button("Bake Scale")) {
          viewport->bakeScale();
          showStatus("Baked scale into model geometry");
        }
        ImGui::EndDisabled();
      }
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

  // Explicit teardown order (viewport/its resources, then the material library, then core
  // resources, then the systems that own them) while the GL context is still current -- mirrors
  // ext/massivepolypusher/demo-suite's own shutdown() ordering.
  delete viewport;
  delete materialLibrary;
  renderSystem->destroyCoreResources();
  delete resourceMgr;
  delete renderSystem;

  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
