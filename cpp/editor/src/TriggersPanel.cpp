#include "TriggersPanel.hpp"

#include <cstdio>

#include "imgui.h"

namespace editor {
namespace {

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

}  // namespace

bool DrawTriggersPanel(EditorState& state, int currentPathIndex) {
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
      // promoted to Finish first, mirroring setTriggerRole's alert-and-revert (js/editor.js:2313-2317).
      ImGui::BeginDisabled(isFinish);
      ImGui::SetNextItemWidth(140);
      roleChanged = ImGui::Combo("Role", &roleIndex, roleNames, 2);
      ImGui::EndDisabled();
      if (roleChanged) role = roleIndex == 1 ? "finish" : "intermediate";
    }

    double width = trigger->width, height = trigger->height, rotation = trigger->rotation;
    bool changed = roleChanged;
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputDouble("Width", &width, 0.0, 0.0, "%.1f", kCommitOnEnter);
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputDouble("Height", &height, 0.0, 0.0, "%.1f", kCommitOnEnter);
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputDouble("Rotation (deg)", &rotation, 0.0, 0.0, "%.1f", kCommitOnEnter);

    int dirIndex = trigger->direction == "forward" ? 1 : (trigger->direction == "backward" ? 2 : 0);
    const char* dirNames[] = {"both", "forward", "backward"};
    ImGui::SetNextItemWidth(140);
    const bool dirChanged = ImGui::Combo("Direction", &dirIndex, dirNames, 3);
    changed |= dirChanged;

    double t = trigger->host.t * 100.0, hostX = trigger->host.x, hostZ = trigger->host.z;
    const bool isPath = trigger->host.kind == "path";
    if (isPath) {
      ImGui::Text("Host path: %s", trigger->host.pathId.c_str());
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("Position (%)", &t, 0.0, 0.0, "%.1f", kCommitOnEnter);
    } else {
      ImGui::Text("Host mesh: %s", trigger->host.meshId.c_str());
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("X", &hostX, 0.0, 0.0, "%.1f", kCommitOnEnter);
      ImGui::SetNextItemWidth(120);
      changed |= ImGui::InputDouble("Z", &hostZ, 0.0, 0.0, "%.1f", kCommitOnEnter);
    }

    if (changed) {
      mutated |= state.editTrigger(id, [&](Trigger& target) {
        target.width = width;
        target.height = height;
        target.rotation = rotation;
        target.direction = dirNames[dirIndex];
        if (isCheckpoint) target.role = role;
        if (target.host.kind == "path") {
          target.host.t = t / 100.0;
        } else {
          target.host.x = hostX;
          target.host.z = hostZ;
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
  for (const auto& t : state.track().triggers) {
    const bool isSelected = selectedId.has_value() && *selectedId == t.id;
    char label[112];
    std::snprintf(label, sizeof(label), "%s  %s%s  host=%s##triggerlist", t.id.c_str(), t.type == "checkpoint" ? "Checkpoint" : "Dummy",
                  t.role == "finish" ? " (Finish)" : "", t.host.kind.c_str());
    if (ImGui::Selectable(label, isSelected)) state.selectTrigger(t.id);
  }

  return mutated;
}

}  // namespace editor
