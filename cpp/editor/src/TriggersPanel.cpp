#include "TriggersPanel.hpp"

#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "PropertiesPanel.hpp"

namespace editor {
namespace {

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

}  // namespace

bool DrawTriggersPanel(EditorState& state, int currentPathIndex, const tox::Track* baked) {
  bool mutated = false;
  const auto& selectedId = state.selectedTriggerId();
  const Trigger* trigger = selectedId.has_value() ? state.findTrigger(*selectedId) : nullptr;

  if (trigger != nullptr) {
    const std::string id = trigger->id;
    const bool isCheckpoint = trigger->type == "checkpoint";
    const bool isFinish = isCheckpoint && trigger->role == "finish";
    ImGui::Text("Trigger: %s (%s)", id.c_str(), isCheckpoint ? "Checkpoint" : "Dummy");

    std::string role = trigger->role;
    bool roleChanged = false;
    if (isCheckpoint) {
      int roleIndex = isFinish ? 1 : 0;
      const char* roleNames[] = {"Intermediate", "Finish"};
      // A trigger already marked Finish can't be demoted here -- another checkpoint must be
      // promoted to Finish first.
      ImGui::BeginDisabled(isFinish);
      ImGui::SetNextItemWidth(140);
      roleChanged = ImGui::Combo("Role", &roleIndex, roleNames, 2);
      ImGui::EndDisabled();
      if (roleChanged) role = roleIndex == 1 ? "finish" : "intermediate";
    }

    const bool isPath = trigger->host.kind == "path";
    int hostPathIndex = -1;
    if (isPath) {
      const auto& paths = state.track().paths;
      for (int i = 0; i < static_cast<int>(paths.size()); ++i)
        if (paths[i].id == trigger->host.pathId) {
          hostPathIndex = i;
          break;
        }
    }

    double width = trigger->width, height = trigger->height, rotation = trigger->rotation;
    bool changed = roleChanged;

    // Auto Width: keeps the trigger's gate width matched to
    // the host path's own baked road width at its host t, recomputed every frame from `baked`
    // rather than a one-time snap -- so it stays in sync whenever a Width control point elsewhere
    // on the track changes and the caller rebakes. Meaningless for a mesh-hosted trigger (no path/
    // t to sample), so the checkbox is disabled and never persisted true there.
    bool autoWidth = isPath && trigger->autoWidth;
    ImGui::BeginDisabled(!isPath);
    if (ImGui::Checkbox("Auto Width (match track)", &autoWidth)) changed = true;
    ImGui::EndDisabled();
    if (autoWidth && hostPathIndex >= 0) {
      const double resolvedWidth = widthAtT(baked, hostPathIndex, state.track().paths[hostPathIndex].closed, trigger->host.t);
      if (std::abs(resolvedWidth - width) > 1e-6) changed = true;
      width = resolvedWidth;
    }

    ImGui::SetNextItemWidth(120);
    ImGui::BeginDisabled(autoWidth);
    changed |= ImGui::InputDouble("Width", &width, 0.0, 0.0, "%.1f", kCommitOnEnter);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputDouble("Height", &height, 0.0, 0.0, "%.1f", kCommitOnEnter);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(200);
    {
      constexpr double kRotationMin = -180.0, kRotationMax = 180.0;
      changed |= ImGui::SliderScalar("Rotation (deg)", ImGuiDataType_Double, &rotation, &kRotationMin, &kRotationMax, "%.1f");
    }

    int dirIndex = trigger->direction == "forward" ? 1 : (trigger->direction == "backward" ? 2 : 0);
    const char* dirNames[] = {"both", "forward", "backward"};
    ImGui::SetNextItemWidth(140);
    const bool dirChanged = ImGui::Combo("Direction", &dirIndex, dirNames, 3);
    changed |= dirChanged;

    // Path-hosted or drivable-mesh-object-hosted (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3.5) --
    // no placement picker exists yet (Milestone 5), so editable but not creatable from this panel;
    // "Add Trigger" below stays path-only.
    const bool isMeshObject = trigger->host.kind == "meshObject";
    double t = trigger->host.t * 100.0, lateral = trigger->host.lateral;
    char meshObjectId[128];
    std::snprintf(meshObjectId, sizeof(meshObjectId), "%s", trigger->host.meshObjectId.c_str());
    double localX = trigger->host.localPosition.x, localY = trigger->host.localPosition.y, localZ = trigger->host.localPosition.z;
    if (isMeshObject) {
      ImGui::SetNextItemWidth(160);
      changed |= ImGui::InputText("Placement id", meshObjectId, sizeof(meshObjectId), kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SetNextItemWidth(90);
      changed |= ImGui::InputDouble("Local X", &localX, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SetNextItemWidth(90);
      changed |= ImGui::InputDouble("Local Y", &localY, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SetNextItemWidth(90);
      changed |= ImGui::InputDouble("Local Z", &localZ, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
    } else {
      ImGui::Text("Host path: %s", trigger->host.pathId.c_str());
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("Position (%)", &t, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("Lateral", &lateral, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
    }

    if (changed) {
      mutated |= state.editTrigger(id, [&](Trigger& target) {
        target.width = width;
        target.height = height;
        target.rotation = rotation;
        target.autoWidth = autoWidth;
        target.direction = dirNames[dirIndex];
        if (isCheckpoint) target.role = role;
        if (target.host.kind == "meshObject") {
          target.host.meshObjectId = meshObjectId;
          target.host.localPosition = tox::Vec3(localX, localY, localZ);
        } else {
          target.host.t = t / 100.0;
          target.host.lateral = lateral;
        }
      });
    }
    ImGui::BeginDisabled(isFinish);
    if (ImGui::Button("Delete Trigger")) {
      if (state.deleteSelectedTrigger()) mutated = true;
    }
    ImGui::EndDisabled();
    if (isFinish) ImGui::TextUnformatted("(designate another Finish first)");
  } else {
    ImGui::TextUnformatted("No trigger selected.");
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Add path trigger:");
  const bool hasPath = currentPathIndex >= 0 && currentPathIndex < static_cast<int>(state.track().paths.size());
  static int typeIndex = 0;  // 0 = Dummy, 1 = Checkpoint
  const char* typeNames[] = {"Dummy", "Checkpoint"};
  ImGui::SetNextItemWidth(120);
  ImGui::Combo("Type", &typeIndex, typeNames, 2);
  static float addTPercent = 50.0f;
  ImGui::SetNextItemWidth(160);
  ImGui::SliderFloat("t (%)##addTriggerT", &addTPercent, 0.0f, 100.0f);
  ImGui::BeginDisabled(!hasPath);
  if (ImGui::Button("Add Trigger")) {
    const std::string type = typeIndex == 1 ? "checkpoint" : "dummy";
    if (state.addPathTrigger(currentPathIndex, type, addTPercent / 100.0).has_value()) mutated = true;
  }
  ImGui::EndDisabled();
  if (!hasPath) ImGui::TextUnformatted("(select or create a path first)");

  ImGui::Separator();
  ImGui::TextUnformatted("Existing triggers:");
  constexpr ImGuiTableFlags kTriggersTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("triggersTable", 5, kTriggersTableFlags)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Host", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("Width", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableHeadersRow();
    for (const auto& t : state.track().triggers) {
      const bool isSelected = selectedId.has_value() && *selectedId == t.id;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      char rowId[80];
      std::snprintf(rowId, sizeof(rowId), "##trigger-%s", t.id.c_str());
      if (ImGui::Selectable(rowId, isSelected, ImGuiSelectableFlags_SpanAllColumns)) state.selectTrigger(t.id);
      ImGui::SameLine();
      ImGui::TextUnformatted(t.id.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%s%s", t.type == "checkpoint" ? "Checkpoint" : "Dummy", t.role == "finish" ? " (Finish)" : "");
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(t.host.kind.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", t.width);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", t.height);
    }
    ImGui::EndTable();
  }

  return mutated;
}

}  // namespace editor
