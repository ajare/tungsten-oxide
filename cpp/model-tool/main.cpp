// cpp/model-tool/main.cpp — model_tool: imports OBJ/FBX/USD/glTF via AssImp, or an existing
// .mppmodel file (see MppModelImport.hpp), previews the result in a live mpp viewport, and saves
// it back out as .mppmodel. See docs/adr/0001-model-tool.md for the design this implements.
//
// GLEW must be the first GL-touching include in this translation unit (before SDL/windows.h),
// matching the rest of this app's GLEW-as-GL-loader choice (ADR D2, include/imgui/imconfig.h).
#include <GL/glew.h>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec3.hpp>
#pragma warning(pop)

#include <algorithm>
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
#include "imgui_impl_sdl3.h"

#include <SDL3/SDL.h>

#include <mpp/Logger.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/ResourceWrangler.h>

#include "FileDialog.hpp"
#include "MaterialLibrary.hpp"
#include "MaterialXmlImport.hpp"
#include "ModelXml.hpp"
#include "MppModelImport.hpp"
#include "MppSave.hpp"
#include "OpenTarget.hpp"
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
  if (!SDL_Init(SDL_INIT_VIDEO)) {
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

  const SDL_WindowFlags windowFlags =
      static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Window* window = SDL_CreateWindow("model_tool", 1280, 800, windowFlags);
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
  SDL_GetWindowSizeInPixels(window, &drawableWidth, &drawableHeight);

  // RenderSystem's constructor calls glewInit() internally (see mpp/src/RenderSystem.cpp) -- no
  // separate glewInit() call needed here, matching every other host of mpp::RenderSystem in this
  // codebase (cpp/launcher, ext/massive-poly-pusher/demo-suite).
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
  // No manual io.Fonts->Build() here: this vendored ImGui's OpenGL3/SDL3 backends use the newer
  // texture-management path (ImGuiBackendFlags_RendererHasTextures, set by ImGui_ImplOpenGL3_Init
  // below), which builds/uploads the atlas lazily on first use.

  ImGui_ImplSDL3_InitForOpenGL(window, glContext);
  ImGui_ImplOpenGL3_Init("#version 130");

  // Status bar message, mirrors cpp/editor/main.cpp's own showStatus() convention: the most recent
  // message replaces whatever's showing and is displayed for 3 seconds.
  std::string statusMessage;
  std::chrono::steady_clock::time_point statusExpiresAt{};
  auto showStatus = [&](std::string message) {
    statusMessage = std::move(message);
    statusExpiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  };

  // Where the currently-loaded model's per-mesh Type/Visible metadata came from. None for a document
  // that came from "Import Model..." (AssImp or a raw .mppmodel with no associated XML) -- Save
  // always falls back to Save As for these, since there's nowhere to write back to yet.
  // StandaloneXml/EmbeddedInTrackResource (both only ever come from "Open...") let Save write back
  // silently to wherever the model was actually opened from, mirroring ordinary "Save" vs "Save As"
  // semantics.
  struct ModelXmlOrigin {
    enum class Kind { None,
                      StandaloneXml,
                      EmbeddedInTrackResource } kind{Kind::None};
    std::filesystem::path xmlPath;
    std::string modelFileReference;  // <ModelFile> text as read, preserved verbatim on save-back
    std::optional<std::string> trackDataReference;
    std::optional<std::string> embeddedModelId;  // set only for EmbeddedInTrackResource
  };
  ModelXmlOrigin modelXmlOrigin;

  // Whether the current model's *geometry* (vertex/index data) differs from what's already on disk
  // at modelXmlOrigin's resolved .mppmodel path -- Save skips rewriting the .mppmodel when this is
  // false, only updating the XML's metadata. Set true by anything that mutates geometry (Bake Scale,
  // and their Undo/Redo, since those also change geometry relative to disk) or by finishing a fresh
  // "Import Model..." (no .mppmodel has ever been written for this document yet, so it always needs
  // one); set false by a successful "Open..." (freshly read from disk, matches by definition) or a
  // successful save (Save or Save As, once the .mppmodel is actually written).
  bool mppModelDirty = false;

  // Set when a picked Track resource XML embeds more than one <Model> -- resolved via the "Choose
  // Model" modal further down the frame loop, mirroring the Material Name Conflicts modal's own
  // OpenPopup-then-BeginPopupModal pattern.
  struct PendingTrackResourceOpen {
    std::filesystem::path xmlPath;
    std::vector<modeltool::TrackResourceModelEntry> entries;
  };
  std::optional<PendingTrackResourceOpen> pendingTrackResourceOpen;
  bool openTrackResourceModelPickerDialog = false;

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
    mpp::ResourceStreamPtr embeddedStream;          // set only for a .mppmodel Embedded conflict
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
    std::vector<std::string> unresolvedMaterialNames;                       // for the finish-up status message
    // Set only when this import came from a <Model> XML (standalone or embedded) -- applied onto
    // the built model's meshes by name once finalizeModelBuild() actually has them, and committed
    // to the outer modelXmlOrigin so Save knows where to write back to.
    std::optional<std::vector<modelxml::MeshMetadataXmlDefinition>> xmlMeshMetadata;
    ModelXmlOrigin xmlOrigin;
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
    const std::optional<std::vector<modelxml::MeshMetadataXmlDefinition>> xmlMeshMetadata = pending.xmlMeshMetadata;
    const ModelXmlOrigin xmlOrigin = pending.xmlOrigin;
    try {
      const std::string defaultFallbackName = materialLibrary->defaultFallbackMaterial()->getName();
      modeltool::BuiltModel built =
          modeltool::buildModel(*resourceMgr, wrangler, std::move(pending.imported), std::move(pending.materialRefs), defaultFallbackName);
      // Per-mesh Type/Visible metadata from the associated <Model> XML, matched onto the just-built
      // meshes by name -- a mesh the XML doesn't mention (e.g. the .mppmodel was re-exported with
      // extra meshes since the XML was last saved) just keeps ImportedMesh's in-memory defaults.
      if (xmlMeshMetadata.has_value()) {
        for (modeltool::ImportedMesh& mesh : built.source.meshes) {
          const auto found = std::find_if(xmlMeshMetadata->begin(), xmlMeshMetadata->end(),
                                          [&](const modelxml::MeshMetadataXmlDefinition& m) { return m.name == mesh.name; });
          if (found != xmlMeshMetadata->end()) {
            mesh.type = found->type;
            mesh.visible = found->visible;
          }
        }
      }
      viewport->setModel(std::move(built));
      modelXmlOrigin = xmlOrigin;
      // A known origin ("Open...") was just read from disk, so it matches by definition; no origin
      // ("Import Model...") means this document's geometry has never been written to a .mppmodel of
      // its own yet, so it always needs a first save.
      mppModelDirty = xmlOrigin.kind == ModelXmlOrigin::Kind::None;
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
                              std::vector<mpp::ResourceStreamPtr> embeddedStreams, std::vector<std::string> unresolvedMaterialNames,
                              std::optional<std::vector<modelxml::MeshMetadataXmlDefinition>> xmlMeshMetadata = std::nullopt,
                              ModelXmlOrigin xmlOrigin = {}) {
    PendingModelImport pending;
    pending.sourceFileForDisplay = sourceFileForDisplay;
    pending.unresolvedMaterialNames = std::move(unresolvedMaterialNames);
    pending.materialRefs.resize(imported.materials.size());
    pending.xmlMeshMetadata = std::move(xmlMeshMetadata);
    pending.xmlOrigin = std::move(xmlOrigin);

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

  // Opens the .mppmodel a <Model> XML fragment (standalone or embedded) references, resolved
  // relative to the XML file's own directory -- mirrors openPath()'s .mppmodel branch, but routes
  // the fragment's mesh metadata + origin through beginModelImport so finalizeModelBuild can apply
  // them once the meshes actually exist.
  auto openModelXml = [&](const std::filesystem::path& xmlPath, const modelxml::ModelXmlDefinition& def, ModelXmlOrigin origin) {
    const std::filesystem::path mppPath = (xmlPath.parent_path() / modeltool::utf8ToWide(def.modelFile)).lexically_normal();
    std::string error;
    std::optional<modeltool::MppModelImportResult> result =
        modeltool::importMppModel(modeltool::pathToUtf8(mppPath), *resourceMgr, *materialLibrary, &error);
    if (!result.has_value()) {
      showStatus("Open failed: " + error);
      return;
    }
    beginModelImport(std::move(result->model), modeltool::pathToUtf8(mppPath), std::move(result->embeddedMaterialStreams),
                     std::move(result->unresolvedMaterialNames), def.meshes, std::move(origin));
  };

  // "Open..." is <Model> XML only -- standalone or embedded in a Track resource. Classifies the
  // picked file: a standalone <Model> XML opens the .mppmodel it references with that fragment's
  // metadata attached; a Track resource XML opens its one embedded <Model>, or queues the "Choose
  // Model" picker modal if it has more than one. Anything else (a bare .mppmodel, or a non-Model-
  // shaped XML) is rejected -- that's "Import Model..."'s job instead.
  auto doOpen = [&]() {
    const modeltool::FileDialogResult picked =
        modeltool::showOpenFileDialog(L"Open Model", {{L"Model XML (*.xml)", L"*.xml"}});
    if (!picked.ok) return;

    const std::filesystem::path path = picked.path;
    const modeltool::OpenTargetKind kind = modeltool::classifyOpenTarget(path);
    try {
      switch (kind) {
        case modeltool::OpenTargetKind::StandaloneModelXml: {
          const modelxml::ModelXmlDefinition def = modelxml::loadStandaloneModelXml(path);
          ModelXmlOrigin origin;
          origin.kind = ModelXmlOrigin::Kind::StandaloneXml;
          origin.xmlPath = path;
          origin.modelFileReference = def.modelFile;
          origin.trackDataReference = def.trackData;
          openModelXml(path, def, std::move(origin));
          break;
        }
        case modeltool::OpenTargetKind::TrackResourceXml: {
          std::vector<modeltool::TrackResourceModelEntry> entries = modeltool::scanTrackResourceModels(path);
          if (entries.empty()) {
            showStatus("Open failed: '" + modeltool::pathToUtf8(path) + "' has no <Model> entries in its Models list.");
          } else if (entries.size() == 1) {
            const modelxml::ModelXmlDefinition def = modeltool::readEmbeddedModel(path, entries.front().id);
            ModelXmlOrigin origin;
            origin.kind = ModelXmlOrigin::Kind::EmbeddedInTrackResource;
            origin.xmlPath = path;
            origin.modelFileReference = def.modelFile;
            origin.trackDataReference = def.trackData;
            origin.embeddedModelId = entries.front().id;
            openModelXml(path, def, std::move(origin));
          } else {
            pendingTrackResourceOpen = PendingTrackResourceOpen{path, std::move(entries)};
            openTrackResourceModelPickerDialog = true;
          }
          break;
        }
        case modeltool::OpenTargetKind::MppModel:
        case modeltool::OpenTargetKind::Unsupported:
          showStatus("Open failed: '" + modeltool::pathToUtf8(path) +
                     "' is not a <Model> XML file -- use Import Model... for a raw .mppmodel or a 3D model file.");
          break;
      }
    } catch (const std::exception& error) {
      showStatus(std::string("Open failed: ") + error.what());
    }
  };

  // "Import Model...": AssImp formats (ADR 0001 D11's combined filter) plus a raw .mppmodel, always
  // as a brand-new document with no <Model> XML origin -- the counterpart to "Open...", for starting
  // from scratch rather than continuing an existing authored Model. Shares openPath()'s dispatch with
  // the CLI-arg auto-import below.
  auto doImportModel = [&]() {
    const modeltool::FileDialogResult picked = modeltool::showOpenFileDialog(
        L"Import Model",
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

  // Always prompts for a destination and writes a raw .mppmodel + companion materials XML --
  // unchanged from before TRACK_MODEL_LIST_PLAN.md Milestone 3.3, used for a fresh AssImp import (no
  // XML origin yet) or an explicit "save a copy elsewhere". doSave() below is the smart one: it
  // writes back to modelXmlOrigin silently when one is known, falling back to this otherwise.
  // Always prompts for a NEW standalone <Model> XML destination (never an embedded-in-Track-resource
  // one -- Save As always creates/targets a standalone file), writes the .mppmodel unconditionally
  // (this is establishing a brand-new save location, so there's always a real file to write there
  // regardless of mppModelDirty), and adopts the new location as modelXmlOrigin so a subsequent
  // Ctrl+S goes there. If the document already had an origin (e.g. "Save a copy elsewhere" of an
  // opened Model), its trackData carries over; a fresh "Import Model..." document has none.
  auto doSaveAs = [&]() {
    if (!viewport->hasModel()) return;
    const modeltool::FileDialogResult picked =
        modeltool::showSaveFileDialog(L"Save Model XML", {{L"Model XML (*.xml)", L"*.xml"}}, L"model.xml", L"xml");
    if (!picked.ok) return;

    const std::filesystem::path xmlPath = picked.path;
    const std::filesystem::path mppPath = std::filesystem::path(xmlPath).replace_extension(L"mppmodel");
    std::string error;
    try {
      if (!modeltool::saveModelAsMppModel(*viewport->builtModel(), *materialLibrary, modeltool::pathToUtf8(mppPath), &error)) {
        showStatus("Save failed: " + error);
        return;
      }

      modelxml::ModelXmlDefinition def;
      def.modelFile = modeltool::pathToUtf8(mppPath.filename());
      def.trackData = modelXmlOrigin.trackDataReference;
      for (const modeltool::ImportedMesh& mesh : viewport->builtModel()->source.meshes) def.meshes.push_back({mesh.name, mesh.type, mesh.visible});
      modelxml::saveStandaloneModelXml(xmlPath, def);

      modelXmlOrigin = ModelXmlOrigin{};
      modelXmlOrigin.kind = ModelXmlOrigin::Kind::StandaloneXml;
      modelXmlOrigin.xmlPath = xmlPath;
      modelXmlOrigin.modelFileReference = def.modelFile;
      modelXmlOrigin.trackDataReference = def.trackData;
      mppModelDirty = false;
      showStatus("Saved " + modeltool::pathToUtf8(xmlPath) + " and " + modeltool::pathToUtf8(mppPath));
    } catch (const std::exception& saveError) {
      showStatus(std::string("Save failed: ") + saveError.what());
    }
  };

  // Writes back to wherever the model was opened from: a document with no known origin (fresh
  // "Import Model...") falls back to doSaveAs() (prompts). A StandaloneXml/EmbeddedInTrackResource
  // origin instead updates the associated <Model> fragment's mesh metadata always, but only re-saves
  // the .mppmodel when mppModelDirty says the geometry has actually changed since it was last
  // written -- a plain metadata-only edit (or no edit at all) leaves the .mppmodel untouched.
  auto doSave = [&]() {
    if (!viewport->hasModel()) return;
    if (modelXmlOrigin.kind == ModelXmlOrigin::Kind::None) {
      doSaveAs();
      return;
    }

    const std::filesystem::path mppPath =
        (modelXmlOrigin.xmlPath.parent_path() / modeltool::utf8ToWide(modelXmlOrigin.modelFileReference)).lexically_normal();
    const bool wroteMppModel = mppModelDirty;
    std::string error;
    try {
      if (wroteMppModel) {
        if (!modeltool::saveModelAsMppModel(*viewport->builtModel(), *materialLibrary, modeltool::pathToUtf8(mppPath), &error)) {
          showStatus("Save failed: " + error);
          return;
        }
      }

      modelxml::ModelXmlDefinition def;
      def.modelFile = modelXmlOrigin.modelFileReference;
      def.trackData = modelXmlOrigin.trackDataReference;
      for (const modeltool::ImportedMesh& mesh : viewport->builtModel()->source.meshes) def.meshes.push_back({mesh.name, mesh.type, mesh.visible});

      if (modelXmlOrigin.kind == ModelXmlOrigin::Kind::StandaloneXml) {
        modelxml::saveStandaloneModelXml(modelXmlOrigin.xmlPath, def);
      } else {
        def.id = modelXmlOrigin.embeddedModelId;
        modeltool::rewriteEmbeddedModel(modelXmlOrigin.xmlPath, *modelXmlOrigin.embeddedModelId, def);
      }
      mppModelDirty = false;
      showStatus("Saved " + modeltool::pathToUtf8(modelXmlOrigin.xmlPath) + " (mppmodel " + (wroteMppModel ? "written" : "unchanged") + ")");
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

  // Reassigns mesh `meshIndex`'s material to `materialName` (an already-loaded MaterialLibrary
  // entry, from the Meshes panel's material combobox below). Reuses an existing
  // built->source.materials[] entry of that name if this model already has one (another mesh
  // already using it); otherwise acquires a fresh MaterialLibrary reference and appends a new
  // entry. Afterward, any materials[] entry no mesh references anymore (this was the reassigned
  // mesh's *last* use of its previous material) is pruned and its reference released -- otherwise
  // MaterialLibrary's "(in use)" indicator and Unload button would stay wrong forever for a
  // material this model no longer actually uses.
  auto assignMeshMaterial = [&](std::size_t meshIndex, const std::string& materialName) {
    modeltool::BuiltModel* current = viewport->mutableBuiltModel();
    if (current == nullptr || meshIndex >= current->source.meshes.size()) return;

    modeltool::ImportedModel updated = current->source;
    std::vector<std::optional<modeltool::MaterialReference>> materialRefs = current->materialRefs;

    const auto existingIt = std::find_if(updated.materials.begin(), updated.materials.end(), [&](const modeltool::ImportedMaterial& m) {
      return m.origin != modeltool::MaterialOrigin::DefaultFallback && m.name == materialName;
    });
    std::size_t newIndex;
    if (existingIt != updated.materials.end()) {
      newIndex = static_cast<std::size_t>(existingIt - updated.materials.begin());
    } else {
      const std::optional<modeltool::MaterialReference> ref = materialLibrary->acquireExistingReference(materialName);
      if (!ref.has_value()) {
        showStatus("Failed to assign material '" + materialName + "' -- not currently loaded.");
        return;
      }
      modeltool::ImportedMaterial fresh;
      fresh.name = materialName;
      fresh.diffuseTexturePath = materialLibrary->materials().at(materialName).texturePath;
      fresh.origin = modeltool::MaterialOrigin::ExternalReference;
      updated.materials.push_back(fresh);
      materialRefs.push_back(ref);
      newIndex = updated.materials.size() - 1;
    }
    updated.meshes[meshIndex].materialIndex = static_cast<int>(newIndex);

    // Prune materials[] entries no mesh references anymore, releasing their MaterialLibrary
    // reference, and remap every mesh's materialIndex to the compacted list.
    std::vector<bool> stillUsed(updated.materials.size(), false);
    for (const modeltool::ImportedMesh& mesh : updated.meshes) stillUsed[static_cast<std::size_t>(mesh.materialIndex)] = true;
    std::vector<int> remap(updated.materials.size(), -1);
    std::vector<modeltool::ImportedMaterial> compactedMaterials;
    std::vector<std::optional<modeltool::MaterialReference>> compactedRefs;
    for (std::size_t i = 0; i < updated.materials.size(); ++i) {
      if (stillUsed[i]) {
        remap[i] = static_cast<int>(compactedMaterials.size());
        compactedMaterials.push_back(updated.materials[i]);
        compactedRefs.push_back(materialRefs[i]);
      } else if (materialRefs[i].has_value()) {
        materialLibrary->releaseModelReference(*materialRefs[i]);
      }
    }
    for (modeltool::ImportedMesh& mesh : updated.meshes) mesh.materialIndex = remap[static_cast<std::size_t>(mesh.materialIndex)];
    updated.materials = std::move(compactedMaterials);

    const std::string meshName = updated.meshes[meshIndex].name;
    current->materialRefs = std::move(compactedRefs);
    viewport->refreshGeometry(std::move(updated));
    mppModelDirty = true;
    showStatus("Mesh '" + meshName + "' material set to '" + materialName + "'");
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
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) running = false;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
        running = false;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Ctrl+O/Ctrl+S/Ctrl+Z/Ctrl+Y, global since there's no text-input widget yet that would need to
    // steal these keys (mirrors cpp/editor/main.cpp's own !io.WantTextInput-guarded shortcut block).
    if (!io.WantTextInput && io.KeyCtrl) {
      if (ImGui::IsKeyPressed(ImGuiKey_O)) doOpen();
      if (ImGui::IsKeyPressed(ImGuiKey_S)) doSave();
      if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (viewport->canUndo()) {
          viewport->undo();
          mppModelDirty = true;
          showStatus("Undo");
        }
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
        if (viewport->canRedo()) {
          viewport->redo();
          mppModelDirty = true;
          showStatus("Redo");
        }
      }
    }
    // G toggles the viewport's reference grid, unmodified (mirrors cpp/editor's own G shortcut for
    // its top-down grid -- TopDownView.hpp/main.cpp).
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_G)) viewport->setGridVisible(!viewport->gridVisible());

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        // "Open..." (Ctrl+O) is <Model> XML only -- standalone or embedded in a Track resource
        // (doOpen() classifies and dispatches, OpenTarget.hpp). "Import Model..." is the AssImp-
        // formats-plus-raw-.mppmodel entry point for starting a brand-new document with no XML
        // origin, letting the user set mesh properties from scratch.
        if (ImGui::MenuItem("Open...", "Ctrl+O")) doOpen();
        if (ImGui::MenuItem("Import Model...")) doImportModel();
        ImGui::Separator();
        // "Save" (Ctrl+S) writes back to wherever the model was opened from when that's known;
        // "Save As" always prompts for a new <Model> XML destination.
        if (ImGui::MenuItem("Save", "Ctrl+S", false, viewport->hasModel())) doSave();
        if (ImGui::MenuItem("Save As...", nullptr, false, viewport->hasModel())) doSaveAs();
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
          mppModelDirty = true;
          showStatus("Undo");
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, viewport->canRedo())) {
          viewport->redo();
          mppModelDirty = true;
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

    // "Choose Model" modal (TRACK_MODEL_LIST_PLAN.md Milestone 3.3): a picked Track resource XML
    // embedded more than one <Model> -- one row per entry, opened via openModelXml() on click.
    if (openTrackResourceModelPickerDialog) {
      ImGui::OpenPopup("Choose Model");
      openTrackResourceModelPickerDialog = false;
    }
    if (ImGui::BeginPopupModal("Choose Model", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("This Track resource embeds more than one Model -- choose which to open:");
      ImGui::Separator();
      std::optional<std::string> selectedModelId;
      if (pendingTrackResourceOpen.has_value()) {
        for (const modeltool::TrackResourceModelEntry& entry : pendingTrackResourceOpen->entries) {
          const std::string label = entry.id + "  (" + entry.modelFileReference + ")";
          if (ImGui::Selectable(label.c_str())) selectedModelId = entry.id;
        }
      }
      ImGui::Separator();
      if (ImGui::Button("Cancel")) {
        pendingTrackResourceOpen.reset();
        ImGui::CloseCurrentPopup();
      }
      if (selectedModelId.has_value()) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();

      // Deferred until after EndPopup() (mirroring the "Material Name Conflicts" modal's own
      // act-then-close pattern) -- xmlPath/modelId are copied out before pendingTrackResourceOpen
      // resets, since openModelXml() below can itself repopulate it (e.g. a Cancel-then-reopen
      // wouldn't, but nothing here should assume pendingTrackResourceOpen still holds this frame's
      // selection afterward).
      if (selectedModelId.has_value() && pendingTrackResourceOpen.has_value()) {
        const std::filesystem::path xmlPath = pendingTrackResourceOpen->xmlPath;
        const std::string modelId = *selectedModelId;
        pendingTrackResourceOpen.reset();
        try {
          const modelxml::ModelXmlDefinition def = modeltool::readEmbeddedModel(xmlPath, modelId);
          ModelXmlOrigin origin;
          origin.kind = ModelXmlOrigin::Kind::EmbeddedInTrackResource;
          origin.xmlPath = xmlPath;
          origin.modelFileReference = def.modelFile;
          origin.trackDataReference = def.trackData;
          origin.embeddedModelId = modelId;
          openModelXml(xmlPath, def, std::move(origin));
        } catch (const std::exception& error) {
          showStatus(std::string("Open failed: ") + error.what());
        }
      }
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
        // Applying a material reassignment rebuilds built->source.meshes wholesale (see
        // assignMeshMaterial()/Viewport::refreshGeometry()) -- deferred until after this loop so
        // that rebuild doesn't invalidate the very range this loop is iterating.
        std::optional<std::pair<std::size_t, std::string>> pendingMeshMaterialChange;
        for (std::size_t meshIndex = 0; meshIndex < built->source.meshes.size(); ++meshIndex) {
          modeltool::ImportedMesh& mesh = built->source.meshes[meshIndex];
          ImGui::PushID(static_cast<int>(meshIndex));
          const modeltool::ImportedMaterial& material = built->source.materials[static_cast<std::size_t>(mesh.materialIndex)];
          ImGui::BulletText("%s (%zu tris)", mesh.name.c_str(), mesh.indices.size() / 3);
          ImGui::Indent();
          // DefaultFallback's own `name` is the original, never-resolved bare material name (see
          // MppModelImport.cpp/AssImpImport.cpp) -- flagged the same way the Materials section
          // below flags it, rather than implying the mesh actually has that material bound. The
          // combobox lists every currently-loaded MaterialLibrary entry regardless of the mesh's
          // current material origin, so a DefaultFallback (or any other) assignment can always be
          // corrected here instead of only by re-exporting from the original source file.
          const char* comboLabel = material.origin == modeltool::MaterialOrigin::DefaultFallback
                                       ? "NOT FOUND -- using default white"
                                       : material.name.c_str();
          if (ImGui::BeginCombo("Material", comboLabel)) {
            for (const auto& [name, loadedMaterial] : materialLibrary->materials()) {
              const bool selected = material.origin != modeltool::MaterialOrigin::DefaultFallback && name == material.name;
              if (ImGui::Selectable(name.c_str(), selected)) pendingMeshMaterialChange = {meshIndex, name};
            }
            if (materialLibrary->materials().empty()) ImGui::TextDisabled("(none loaded -- Import Materials XML..., or open a model)");
            ImGui::EndCombo();
          }
          // Per-mesh Type/Visible metadata (TRACK_MODEL_LIST_PLAN.md Milestone 3.4), written to the
          // associated <Model> XML on Save (OpenTarget.hpp), not into the .mppmodel's own mesh name.
          int typeIndex = static_cast<int>(mesh.type);
          if (ImGui::Combo("Type", &typeIndex, "Track\0Physical\0Decorative\0"))
            mesh.type = static_cast<modelxml::MeshType>(typeIndex);
          if (mesh.type == modelxml::MeshType::Track)
            ImGui::TextDisabled("    requires a TrackData file on this Model");
          ImGui::Checkbox("Visible", &mesh.visible);
          ImGui::Unindent();
          ImGui::PopID();
        }
        if (pendingMeshMaterialChange.has_value()) assignMeshMaterial(pendingMeshMaterialChange->first, pendingMeshMaterialChange->second);
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
          mppModelDirty = true;
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
    SDL_GetWindowSizeInPixels(window, &drawableWidth, &drawableHeight);
    glViewport(0, 0, drawableWidth, drawableHeight);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  // Explicit teardown order (viewport/its resources, then the material library, then core
  // resources, then the systems that own them) while the GL context is still current -- mirrors
  // ext/massive-poly-pusher/demo-suite's own shutdown() ordering.
  delete viewport;
  delete materialLibrary;
  renderSystem->destroyCoreResources();
  delete resourceMgr;
  delete renderSystem;

  SDL_GL_DestroyContext(glContext);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
