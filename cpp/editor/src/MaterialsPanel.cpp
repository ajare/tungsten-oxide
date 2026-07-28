#include "MaterialsPanel.hpp"

#include "imgui.h"

namespace editor {

bool DrawMaterialsPanel(EditorState& state, const MaterialCatalog& materialCatalog, TextureCache& textures, int currentPathIndex) {
  bool mutated = false;
  const bool hasCurrentPath = currentPathIndex >= 0 && currentPathIndex < static_cast<int>(state.track().paths.size());
  const std::optional<std::string> currentMaterial =
      hasCurrentPath ? std::optional<std::string>(state.track().paths[currentPathIndex].material) : std::nullopt;

  if (materialCatalog.materials().empty()) {
    ImGui::TextUnformatted("No TrackMaterials loaded.");
    return mutated;
  }

  constexpr float kThumbDisplay = 48.0f;
  for (const MaterialEntry& entry : materialCatalog.materials()) {
    ImGui::PushID(entry.qualifiedName.c_str());

    const bool selected = currentMaterial.has_value() && *currentMaterial == entry.qualifiedName;
    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

    bool clicked = false;
    const LoadedTexture& thumb = entry.texturePaths.empty() ? LoadedTexture{} : textures.get(entry.texturePaths.front());
    if (thumb.ok()) {
      clicked = ImGui::ImageButton("thumb", static_cast<ImTextureID>(thumb.glId), ImVec2(kThumbDisplay, kThumbDisplay));
    } else {
      clicked = ImGui::Button("(no preview)", ImVec2(kThumbDisplay, kThumbDisplay));
    }
    if (selected) ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextUnformatted(entry.qualifiedName.c_str());

    if (clicked && hasCurrentPath && state.assignPathMaterial(currentPathIndex, entry.qualifiedName)) mutated = true;

    ImGui::PopID();
  }

  return mutated;
}

}  // namespace editor
