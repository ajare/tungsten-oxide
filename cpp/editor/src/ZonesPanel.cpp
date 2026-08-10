#include "ZonesPanel.hpp"

#include <cstdio>

#include "imgui.h"

namespace editor {
namespace {

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

}  // namespace

bool DrawZonesPanel(EditorState& state, int currentPathIndex) {
  bool mutated = false;
  const auto& selectedId = state.selectedZoneId();
  const Zone* zone = selectedId.has_value() ? state.findZone(*selectedId) : nullptr;

  if (zone != nullptr) {
    const std::string id = zone->id;
    const char* effectName = zone->effect == "startGrid" ? "Start Grid" : zone->effect == "jump" ? "Jump" : "Boost";
    ImGui::Text("Zone: %s (%s)", id.c_str(), effectName);

    double width = zone->width, length = zone->length;
    bool changed = false;
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputDouble("Width", &width, 0.0, 0.0, "%.1f", kCommitOnEnter);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputDouble("Length", &length, 0.0, 0.0, "%.1f", kCommitOnEnter);
    changed |= ImGui::IsItemDeactivatedAfterEdit();

    double factor = zone->factor, duration = zone->duration;
    if (zone->effect == "velocityChange") {
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("Boost x maxSpeed", &factor, 0.0, 0.0, "%.2f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("Duration (s)", &duration, 0.0, 0.0, "%.2f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
    }

    // Path-hosted or drivable-mesh-object-hosted (DRIVABLE_MESH_OBJECTS_PLAN.md Milestone 3.5) --
    // no placement picker exists yet (Milestone 5), so a meshObject-hosted zone can be edited (its
    // meshObjectId retyped, its local offset adjusted) but not created from this panel; that's why
    // "Add Zone" below stays path-only.
    const bool isMeshObject = zone->host.kind == "meshObject";
    double t = zone->host.t * 100.0, lateral = zone->host.lateral;
    char meshObjectId[128];
    std::snprintf(meshObjectId, sizeof(meshObjectId), "%s", zone->host.meshObjectId.c_str());
    double localX = zone->host.localPosition.x, localY = zone->host.localPosition.y, localZ = zone->host.localPosition.z;
    double localYaw = zone->host.localYaw;
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
      ImGui::SetNextItemWidth(90);
      changed |= ImGui::InputDouble("Local Yaw (deg)", &localYaw, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
    } else {
      ImGui::Text("Host path: %s", zone->host.pathId.c_str());
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("T (%)", &t, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("Lateral", &lateral, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
    }

    if (changed) {
      mutated |= state.editZone(id, [&](Zone& target) {
        target.width = width;
        target.length = length;
        if (target.effect == "velocityChange") {
          target.factor = factor;
          target.duration = duration;
        }
        if (target.host.kind == "meshObject") {
          target.host.meshObjectId = meshObjectId;
          target.host.localPosition = tox::Vec3(localX, localY, localZ);
          target.host.localYaw = localYaw;
        } else {
          target.host.t = t / 100.0;
          target.host.lateral = lateral;
        }
      });
    }
    if (ImGui::Button("Delete Zone")) {
      if (state.deleteSelectedZone()) mutated = true;
    }
  } else {
    ImGui::TextUnformatted("No zone selected.");
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Add path zone:");
  const bool hasPath = currentPathIndex >= 0 && currentPathIndex < static_cast<int>(state.track().paths.size());
  static int effectIndex = 0;  // 0 = Boost, 1 = Jump, 2 = Start Grid
  const char* effectNames[] = {"Boost", "Jump", "Start Grid"};
  ImGui::SetNextItemWidth(120);
  ImGui::Combo("Effect", &effectIndex, effectNames, 3);
  static float addTPercent = 50.0f;
  ImGui::SetNextItemWidth(160);
  ImGui::SliderFloat("t (%)##addZoneT", &addTPercent, 0.0f, 100.0f);
  ImGui::BeginDisabled(!hasPath);
  if (ImGui::Button("Add Zone")) {
    const std::string effect = effectIndex == 1 ? "jump" : effectIndex == 2 ? "startGrid" : "velocityChange";
    if (state.addPathZone(currentPathIndex, effect, addTPercent / 100.0, 0.0).has_value()) mutated = true;
  }
  ImGui::EndDisabled();
  if (!hasPath) ImGui::TextUnformatted("(select or create a path first)");

  ImGui::Separator();
  ImGui::TextUnformatted("Existing zones:");
  constexpr ImGuiTableFlags kZonesTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("zonesTable", 5, kZonesTableFlags)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Host", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("Width", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableHeadersRow();
    for (const auto& z : state.track().zones) {
      const bool isSelected = selectedId.has_value() && *selectedId == z.id;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      char rowId[80];
      std::snprintf(rowId, sizeof(rowId), "##zone-%s", z.id.c_str());
      if (ImGui::Selectable(rowId, isSelected, ImGuiSelectableFlags_SpanAllColumns)) state.selectZone(z.id);
      ImGui::SameLine();
      ImGui::TextUnformatted(z.id.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(z.effect == "startGrid" ? "Start Grid" : z.effect == "jump" ? "Jump" : "Boost");
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(z.host.kind.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", z.width);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", z.length);
    }
    ImGui::EndTable();
  }

  return mutated;
}

}  // namespace editor
