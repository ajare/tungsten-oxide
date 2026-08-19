#include "HandlingPanel.hpp"

#include "imgui.h"

namespace editor {
namespace {

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

}  // namespace

bool DrawHandlingPanel(EditorState& state) {
  bool mutated = false;
  const Handling& h = state.track().handling;
  double maxSpeed = h.maxSpeed, accel = h.accel, turnSpeed = h.turnSpeed, weight = h.weight;
  bool changed = false;

  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Max Speed (m/s)", &maxSpeed, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Acceleration", &accel, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Turn Speed (deg/s)", &turnSpeed, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Weight (kg)", &weight, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  if (changed) {
    state.setHandling(maxSpeed, accel, turnSpeed, weight);
    mutated = true;
  }

  if (ImGui::Button("Reset to Default")) {
    state.resetHandling();
    mutated = true;
  }

  return mutated;
}

}  // namespace editor
