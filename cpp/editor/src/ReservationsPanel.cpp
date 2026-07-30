#include "ReservationsPanel.hpp"

#include <cstdio>

#include "imgui.h"

namespace editor {
namespace {

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

}  // namespace

bool DrawReservationsPanel(EditorState& state, int currentPathIndex) {
  bool mutated = false;
  const auto& selectedId = state.selectedReservationId();
  const Reservation* reservation = selectedId.has_value() ? state.findReservation(*selectedId) : nullptr;

  if (reservation != nullptr) {
    const std::string id = reservation->id;
    ImGui::Text("Reservation: %s", id.c_str());

    double t0 = reservation->t0 * 100.0, t1 = reservation->t1 * 100.0, width = reservation->width;
    bool changed = false;
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputDouble("t0 (%)", &t0, 0.0, 0.0, "%.1f", kCommitOnEnter);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputDouble("t1 (%)", &t1, 0.0, 0.0, "%.1f", kCommitOnEnter);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputDouble("Width", &width, 0.0, 0.0, "%.1f", kCommitOnEnter);
    changed |= ImGui::IsItemDeactivatedAfterEdit();

    if (changed) {
      mutated |= state.editReservation(id, [&](Reservation& target) {
        target.t0 = t0 / 100.0;
        target.t1 = t1 / 100.0;
        target.width = width;
      });
    }
    if (ImGui::Button("Delete Reservation")) {
      if (state.deleteSelectedReservation()) mutated = true;
    }
  } else {
    ImGui::TextUnformatted("No reservation selected.");
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Add reservation on current path:");
  const bool hasPath = currentPathIndex >= 0 && currentPathIndex < static_cast<int>(state.track().paths.size());
  static float addT0Percent = 30.0f, addT1Percent = 70.0f;
  static float addWidth = 8.0f;
  ImGui::SetNextItemWidth(160);
  ImGui::SliderFloat("t0 (%)##addReservationT0", &addT0Percent, 0.0f, 100.0f);
  ImGui::SetNextItemWidth(160);
  ImGui::SliderFloat("t1 (%)##addReservationT1", &addT1Percent, 0.0f, 100.0f);
  ImGui::SetNextItemWidth(120);
  ImGui::InputFloat("Width##addReservationWidth", &addWidth, 0.0f, 0.0f, "%.1f");
  ImGui::BeginDisabled(!hasPath);
  if (ImGui::Button("Add Reservation")) {
    if (state.addReservation(currentPathIndex, addT0Percent / 100.0, addT1Percent / 100.0, addWidth).has_value())
      mutated = true;
  }
  ImGui::EndDisabled();
  if (!hasPath) ImGui::TextUnformatted("(select or create a path first)");

  ImGui::Separator();
  ImGui::TextUnformatted("Existing reservations:");
  constexpr ImGuiTableFlags kReservationsTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("reservationsTable", 5, kReservationsTableFlags)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("t0 (%)", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("t1 (%)", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("Width", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableHeadersRow();
    for (const auto& path : state.track().paths) {
      for (const auto& r : path.reservations) {
        const bool isSelected = selectedId.has_value() && *selectedId == r.id;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        char rowId[80];
        std::snprintf(rowId, sizeof(rowId), "##reservation-%s", r.id.c_str());
        if (ImGui::Selectable(rowId, isSelected, ImGuiSelectableFlags_SpanAllColumns)) state.selectReservation(r.id);
        ImGui::SameLine();
        ImGui::TextUnformatted(r.id.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(path.id.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", r.t0 * 100.0);
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", r.t1 * 100.0);
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", r.width);
      }
    }
    ImGui::EndTable();
  }

  return mutated;
}

}  // namespace editor
