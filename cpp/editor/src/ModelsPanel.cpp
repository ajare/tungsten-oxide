#include "ModelsPanel.hpp"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

namespace editor {
namespace {

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

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

  ImGui::TextUnformatted("Selecting a row selects one of its placements (Model Placements panel).");
  ImGui::TextUnformatted("ModelFile must be a path relative to this Track's own save location (no drive letter, no '..') -- Save will refuse an unsafe one.");
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
      // Editable (TRACK_MODEL_LIST_PLAN.md follow-up): "Load Model..." on a not-yet-saved Track has
      // no save directory to resolve a relative reference against, so it falls back to storing the
      // picked file's absolute path (main.cpp) -- this is the promised way to retype it into a real
      // relative one afterwards, since there's otherwise no in-editor path back from that fallback.
      char modelFile[512];
      std::snprintf(modelFile, sizeof(modelFile), "%s", model.modelFile.c_str());
      ImGui::SetNextItemWidth(-1);
      std::string fieldId = "##modelFile-" + id;
      bool changed = ImGui::InputText(fieldId.c_str(), modelFile, sizeof(modelFile), kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      if (changed) {
        const std::string newValue = modelFile;
        state.editEmbeddedModel(id, [&](modelxml::ModelXmlDefinition& m) { m.modelFile = newValue; });
      }
      ImGui::TableNextColumn();
      ImGui::Text("%zu", model.meshes.size());
      ImGui::TableNextColumn();
      ImGui::Text("%d", placementCountFor(state, id));
    }
    ImGui::EndTable();
  }
}

}  // namespace editor
