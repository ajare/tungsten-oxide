#include "ModelPlacementsPanel.hpp"

#include <cstdio>

#include "ModelXml.hpp"
#include "imgui.h"

namespace editor {
namespace {

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

// Model placement fields (originally DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 5.4; `modelId` now
// names an embedded <Model id> rather than a raw path, and gained a per-mesh Type/Visible editor --
// TRACK_MODEL_LIST_PLAN.md Milestone 6.2): position/rotation/scale, a `modelId` text field (still
// unvalidated against anything -- retyping it to a different embedded Model's id is a valid,
// if manual, way to repoint a placement), and the referenced Model's own mesh metadata.
void drawSelectedPlacementFields(EditorState& state, const std::string& id, bool& mutated) {
  const ModelPlacement* placement = state.findMeshObjectPlacement(id);
  if (placement == nullptr) return;
  ImGui::Text("Model Placement: %s", id.c_str());

  char modelId[256];
  std::snprintf(modelId, sizeof(modelId), "%s", placement->modelId.c_str());
  bool changed = false;
  ImGui::SetNextItemWidth(280);
  changed |= ImGui::InputText("Model id", modelId, sizeof(modelId), kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();

  double x = placement->position.x, y = placement->position.y, z = placement->position.z;
  ImGui::TextUnformatted("Position");
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("X##moX", &x, 0.0, 0.0, "%.2f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("Y##moY", &y, 0.0, 0.0, "%.2f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("Z##moZ", &z, 0.0, 0.0, "%.2f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();

  double yaw = placement->rotation.x, pitch = placement->rotation.y, roll = placement->rotation.z;
  ImGui::TextUnformatted("Rotation (deg)");
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("Yaw##moYaw", &yaw, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("Pitch##moPitch", &pitch, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("Roll##moRoll", &roll, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();

  double scaleX = placement->scale.x, scaleY = placement->scale.y, scaleZ = placement->scale.z;
  ImGui::TextUnformatted("Scale");
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("X##moScaleX", &scaleX, 0.0, 0.0, "%.2f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("Y##moScaleY", &scaleY, 0.0, 0.0, "%.2f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  changed |= ImGui::InputDouble("Z##moScaleZ", &scaleZ, 0.0, 0.0, "%.2f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();

  if (changed) {
    mutated |= state.editMeshObjectPlacement(id, [&](ModelPlacement& p) {
      p.modelId = modelId;
      p.position = tox::Vec3(x, y, z);
      p.rotation = tox::Vec3(yaw, pitch, roll);
      p.scale = tox::Vec3(scaleX, scaleY, scaleZ);
    });
  }

  // Per-mesh Type/Visible metadata (TRACK_MODEL_LIST_PLAN.md Milestone 6.2): editing here affects
  // every placement referencing the same embedded Model, since the metadata belongs to the shared
  // Model entry, not this one placement -- consistent with "embedded Model is the shared metadata
  // set, placements only add a transform."
  const modelxml::ModelXmlDefinition* model = state.findModel(placement->modelId);
  if (model != nullptr && !model->meshes.empty()) {
    ImGui::Separator();
    ImGui::Text("Model meshes (shared across every placement of %s)", placement->modelId.c_str());
    for (std::size_t i = 0; i < model->meshes.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      const modelxml::MeshMetadataXmlDefinition& mesh = model->meshes[i];
      ImGui::BulletText("%s", mesh.name.c_str());
      int typeIndex = static_cast<int>(mesh.type);
      bool meshChanged = false;
      ImGui::SetNextItemWidth(150);
      meshChanged |= ImGui::Combo("Type##meshType", &typeIndex, "Track\0Physical\0Decorative\0");
      bool visible = mesh.visible;
      ImGui::SameLine();
      meshChanged |= ImGui::Checkbox("Visible##meshVisible", &visible);
      if (typeIndex == static_cast<int>(modelxml::MeshType::Track))
        ImGui::TextDisabled("  requires a TrackData file on this Model");
      if (meshChanged) {
        mutated |= state.editEmbeddedModel(placement->modelId, [&](modelxml::ModelXmlDefinition& m) {
          m.meshes[i].type = static_cast<modelxml::MeshType>(typeIndex);
          m.meshes[i].visible = visible;
        });
      }
      ImGui::PopID();
    }
  } else if (!placement->modelId.empty()) {
    ImGui::TextDisabled("No mesh metadata embedded for this Model yet.");
  }

  if (ImGui::Button("Delete Placement")) {
    if (state.deleteSelectedMeshObjectPlacement()) mutated = true;
  }
}

}  // namespace

bool DrawModelPlacementsPanel(EditorState& state) {
  bool mutated = false;
  if (state.selectedMeshObjectId().has_value()) {
    drawSelectedPlacementFields(state, *state.selectedMeshObjectId(), mutated);
  } else {
    ImGui::TextUnformatted("No placement selected.");
  }

  if (state.track().meshObjects.empty()) return mutated;

  ImGui::Separator();
  ImGui::TextUnformatted("Right-click the canvas > Place Model to add one.");
  constexpr ImGuiTableFlags kPlacementsTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("modelPlacementsTable", 3, kPlacementsTableFlags)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Model");
    ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableHeadersRow();
    for (const auto& placement : state.track().meshObjects) {
      const bool isSelected = state.selectedMeshObjectId().has_value() && *state.selectedMeshObjectId() == placement.id;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      char rowId[96];
      std::snprintf(rowId, sizeof(rowId), "##placement-%s", placement.id.c_str());
      if (ImGui::Selectable(rowId, isSelected, ImGuiSelectableFlags_SpanAllColumns)) state.selectMeshObject(placement.id);
      ImGui::SameLine();
      ImGui::TextUnformatted(placement.id.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(placement.modelId.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%.1f, %.1f, %.1f", placement.position.x, placement.position.y, placement.position.z);
    }
    ImGui::EndTable();
  }

  return mutated;
}

}  // namespace editor
