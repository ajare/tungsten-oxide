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
    ImGui::Text("Zone: %s (%s)", id.c_str(), zone->effect == "startGrid" ? "Start Grid" : "Boost");

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

    double t = zone->host.t * 100.0, lateral = zone->host.lateral;
    double hostX = zone->host.x, hostZ = zone->host.z, rotation = zone->host.rotation;
    const bool isPath = zone->host.kind == "path";
    if (isPath) {
      ImGui::Text("Host path: %s", zone->host.pathId.c_str());
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("T (%)", &t, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("Lateral", &lateral, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
    } else {
      ImGui::Text("Host mesh: %s", zone->host.meshId.c_str());
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("X", &hostX, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("Z", &hostZ, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("Rotation (deg)", &rotation, 0.0, 0.0, "%.1f", kCommitOnEnter);
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
        if (target.host.kind == "path") {
          target.host.t = t / 100.0;
          target.host.lateral = lateral;
        } else {
          target.host.x = hostX;
          target.host.z = hostZ;
          target.host.rotation = rotation;
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
  ImGui::TextUnformatted("Add path zone (boost pad / start grid):");
  const bool hasPath = currentPathIndex >= 0 && currentPathIndex < static_cast<int>(state.track().paths.size());
  static int effectIndex = 0;  // 0 = Boost, 1 = Start Grid
  const char* effectNames[] = {"Boost", "Start Grid"};
  ImGui::SetNextItemWidth(120);
  ImGui::Combo("Effect", &effectIndex, effectNames, 2);
  static float addTPercent = 50.0f;
  ImGui::SetNextItemWidth(160);
  ImGui::SliderFloat("t (%)##addZoneT", &addTPercent, 0.0f, 100.0f);
  ImGui::BeginDisabled(!hasPath);
  if (ImGui::Button("Add Zone")) {
    const std::string effect = effectIndex == 1 ? "startGrid" : "velocityChange";
    if (state.addPathZone(currentPathIndex, effect, addTPercent / 100.0, 0.0).has_value()) mutated = true;
  }
  ImGui::EndDisabled();
  if (!hasPath) ImGui::TextUnformatted("(select or create a path first)");

  ImGui::Separator();
  ImGui::TextUnformatted("Existing zones:");
  for (const auto& z : state.track().zones) {
    const bool isSelected = selectedId.has_value() && *selectedId == z.id;
    char label[96];
    std::snprintf(label, sizeof(label), "%s  %s  host=%s##zonelist", z.id.c_str(), z.effect == "startGrid" ? "Start Grid" : "Boost",
                 z.host.kind.c_str());
    if (ImGui::Selectable(label, isSelected)) state.selectZone(z.id);
  }

  return mutated;
}

}  // namespace editor
