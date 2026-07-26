#include "TexturePanel.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "nlohmann/json.hpp"

#include "FileDialog.hpp"

namespace editor {

int loadBundledTextureAssets(EditorState& state) {
  const std::filesystem::path assetsDir = findAssetsDir();
  if (assetsDir.empty()) return 0;

  std::ifstream input(assetsDir / "track" / "manifest.json", std::ios::binary);
  if (!input) return 0;
  std::ostringstream buffer;
  buffer << input.rdbuf();

  nlohmann::json manifest;
  try {
    manifest = nlohmann::json::parse(buffer.str());
  } catch (const std::exception&) {
    return 0;
  }
  if (!manifest.is_array()) return 0;

  std::set<std::string> existingPaths;
  for (const auto& [id, asset] : state.track().textureAssets) existingPaths.insert(asset.path);

  int added = 0;
  for (const auto& entry : manifest) {
    if (!entry.is_string()) continue;
    const std::string filename = entry.get<std::string>();
    if (filename.empty()) continue;
    const std::string relativePath = (std::filesystem::path("assets") / "track" / filename).generic_string();
    if (existingPaths.count(relativePath)) continue;
    int width = 0, height = 0;
    if (!readImageSize(assetsDir / "track" / filename, width, height)) continue;
    state.addTextureAsset(textureNameFromPath(filename), relativePath, width, height);
    existingPaths.insert(relativePath);
    ++added;
  }
  return added;
}

bool DrawTexturePanel(EditorState& state, TextureCache& textures, int currentPathIndex) {
  bool mutated = false;
  const bool hasCurrentPath = currentPathIndex >= 0 && currentPathIndex < static_cast<int>(state.track().paths.size());

  if (ImGui::Button("Load Bundled Textures")) {
    if (loadBundledTextureAssets(state) > 0) mutated = true;
  }
  ImGui::SameLine();
  // Mirrors editor.html's #browseTextureBtn (EDITOR_NATIVE_FILE_IO_PLAN.md M10) -- M7b already
  // built readImageSize/addTextureAsset, so this is almost entirely wiring. Unlike editor.js's
  // texturePreviewUrls bookkeeping for files outside assets/track/, TextureCache reads by path
  // lazily on demand, so whatever the dialog returns is stored as TextureAsset.path directly.
  // Survives across frames until the next Browse click (single texture panel instance, same
  // pattern as TopDownCanvas.cpp's right-click context-menu state) -- previously a readImageSize
  // failure (corrupt/unsupported file) did nothing at all: no asset added, no status shown
  // (EDITOR_PARITY_FIXES.md finding 9).
  static std::string browseStatus;
  if (ImGui::Button("Browse...")) {
    const editor::FileDialogResult picked = editor::showOpenFileDialog(
        L"Open Texture Image", {{L"Images (*.png;*.jpg;*.jpeg;*.bmp)", L"*.png;*.jpg;*.jpeg;*.bmp"}});
    if (picked.ok) {
      int width = 0, height = 0;
      if (readImageSize(picked.path, width, height)) {
        // pathToUtf8, not path.string(): the latter narrows through the system ANSI codepage and
        // mangles non-ASCII paths before they ever reach TextureAsset.path / stbi_load
        // (EDITOR_PARITY_FIXES.md finding 7).
        state.addTextureAsset(textureNameFromPath(editor::pathToUtf8(picked.path.filename())), editor::pathToUtf8(picked.path), width, height);
        mutated = true;
        browseStatus.clear();
      } else {
        browseStatus = "Could not read image size: " + editor::pathToUtf8(picked.path);
      }
    }
  }
  if (!browseStatus.empty()) {
    ImGui::SameLine();
    ImGui::TextUnformatted(browseStatus.c_str());
  }
  ImGui::Separator();

  if (state.track().textureAssets.empty()) {
    ImGui::TextUnformatted("No texture images loaded.");
    return mutated;
  }

  const std::optional<TextureBinding> currentBinding =
      hasCurrentPath ? state.track().paths[currentPathIndex].texture : std::nullopt;

  // Snapshot ids up front: deleteTextureAsset mutates track().textureAssets, which would
  // invalidate an in-progress map iterator.
  std::vector<std::string> assetIds;
  for (const auto& [id, asset] : state.track().textureAssets) assetIds.push_back(id);

  for (const std::string& assetId : assetIds) {
    const auto it = state.track().textureAssets.find(assetId);
    if (it == state.track().textureAssets.end()) continue;  // deleted earlier this frame
    const TextureAsset& asset = it->second;

    ImGui::PushID(assetId.c_str());
    ImGui::TextUnformatted(asset.name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
      if (state.deleteTextureAsset(assetId)) mutated = true;
      ImGui::PopID();
      continue;
    }

    const int cols = asset.tileWidth > 0 ? std::max(1, asset.width / asset.tileWidth) : 1;
    const int rows = asset.tileHeight > 0 ? std::max(1, asset.height / asset.tileHeight) : 1;
    const int count = cols * rows;
    ImGui::Text("Path: %s", asset.path.c_str());
    ImGui::Text("Image: %d x %d px  Tiles: %d (%d x %d)", asset.width, asset.height, count, cols, rows);

    int tileWidth = asset.tileWidth;
    ImGui::SetNextItemWidth(100);
    bool tileWidthChanged = ImGui::InputInt("Tile W", &tileWidth, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
    tileWidthChanged |= ImGui::IsItemDeactivatedAfterEdit();
    if (tileWidthChanged) {
      if (state.setTextureTileSize(assetId, true, tileWidth)) mutated = true;
    }
    ImGui::SameLine();
    int tileHeight = asset.tileHeight;
    ImGui::SetNextItemWidth(100);
    bool tileHeightChanged = ImGui::InputInt("Tile H", &tileHeight, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
    tileHeightChanged |= ImGui::IsItemDeactivatedAfterEdit();
    if (tileHeightChanged) {
      if (state.setTextureTileSize(assetId, false, tileHeight)) mutated = true;
    }

    const LoadedTexture& loaded = textures.get(asset.path);
    if (loaded.ok()) {
      constexpr float kTileDisplay = 48.0f;
      for (int tile = 0; tile < count; ++tile) {
        if (tile % cols != 0) ImGui::SameLine();
        const int tx = tile % cols, ty = tile / cols;
        const ImVec2 uv0(static_cast<float>(tx) / static_cast<float>(cols), static_cast<float>(ty) / static_cast<float>(rows));
        const ImVec2 uv1(static_cast<float>(tx + 1) / static_cast<float>(cols), static_cast<float>(ty + 1) / static_cast<float>(rows));
        const bool selected = currentBinding.has_value() && currentBinding->assetId == assetId && currentBinding->tile == tile;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushID(tile);
        const bool clicked =
            ImGui::ImageButton("tile", static_cast<ImTextureID>(loaded.glId), ImVec2(kTileDisplay, kTileDisplay), uv0, uv1);
        ImGui::PopID();
        if (selected) ImGui::PopStyleColor();
        if (clicked && hasCurrentPath && state.assignPathTexture(currentPathIndex, assetId, tile)) mutated = true;
      }
    } else {
      ImGui::TextUnformatted("(image failed to load)");
    }

    ImGui::PopID();
    ImGui::Separator();
  }

  if (hasCurrentPath && currentBinding.has_value()) {
    if (ImGui::Button("Clear Current Path's Texture")) {
      if (state.clearPathTexture(currentPathIndex)) mutated = true;
    }
  }

  return mutated;
}

}  // namespace editor
