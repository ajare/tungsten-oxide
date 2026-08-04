#include "ModelsPanel.hpp"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

namespace editor {
namespace {

int placementCountFor(const EditorState& state, const std::string& modelId) {
  return static_cast<int>(std::count_if(state.track().meshObjects.begin(), state.track().meshObjects.end(),
                                        [&](const ModelPlacement& p) { return p.modelId == modelId; }));
}

// First placement referencing `modelId`, or nullptr if the Model is currently unused by anything on
// the track (e.g. just embedded via "Load Model...", or every placement that used to reference it
// was since deleted).
const ModelPlacement* firstPlacementFor(const EditorState& state, const std::string& modelId) {
  for (const auto& placement : state.track().meshObjects)
    if (placement.modelId == modelId) return &placement;
  return nullptr;
}

}  // namespace

void DrawModelsPanel(EditorState& state) {
  const auto& models = state.track().models;
  if (models.empty()) {
    ImGui::TextDisabled("No Models embedded yet -- File > Load Model...");
    return;
  }

  ImGui::TextUnformatted("Selecting a row selects one of its placements (Point Properties panel).");
  constexpr ImGuiTableFlags kModelsTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("modelsTable", 4, kModelsTableFlags)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("ModelFile");
    ImGui::TableSetupColumn("Meshes", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("Placements", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableHeadersRow();
    for (const auto& model : models) {
      const std::string id = model.id.value_or(std::string("(no id)"));
      const ModelPlacement* placement = firstPlacementFor(state, id);
      const bool isSelected = placement != nullptr && state.selectedMeshObjectId().has_value() && *state.selectedMeshObjectId() == placement->id;

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      char rowId[96];
      std::snprintf(rowId, sizeof(rowId), "##model-%s", id.c_str());
      ImGui::BeginDisabled(placement == nullptr);
      if (ImGui::Selectable(rowId, isSelected, ImGuiSelectableFlags_SpanAllColumns)) state.selectMeshObject(placement->id);
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::TextUnformatted(id.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(model.modelFile.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%zu", model.meshes.size());
      ImGui::TableNextColumn();
      ImGui::Text("%d", placementCountFor(state, id));
    }
    ImGui::EndTable();
  }
}

}  // namespace editor
