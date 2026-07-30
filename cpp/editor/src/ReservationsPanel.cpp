#include "ReservationsPanel.hpp"

#include <algorithm>
#include <cstdio>

#include "PropertiesPanel.hpp"
#include "imgui.h"

namespace editor {
namespace {

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

constexpr const char* kEndCapStyleNames[] = {"Joined", "Mitred", "Rounded"};
constexpr const char* kWidthModeNames[] = {"Fixed (m)", "Percent of road (%)"};

// Converts `width` in place when the mode toggle flips, so the reservation's actual size stays
// (approximately) put rather than being silently reinterpreted under the new unit -- e.g. "8"
// meaning 8 m suddenly meaning 8% would shrink an authored reservation to a sliver. `roadWidthHere`
// is the road's own width at the reservation's midpoint (0 if unknown, e.g. no baked track yet),
// in which case the raw number is carried over unconverted as the least-surprising fallback.
void ConvertWidthForMode(double& width, ReservationWidthMode fromMode, ReservationWidthMode toMode, double roadWidthHere) {
  if (fromMode == toMode || roadWidthHere <= 0.0) return;
  width = toMode == ReservationWidthMode::Percent ? std::clamp(width / roadWidthHere * 100.0, 0.0, 100.0) : width / 100.0 * roadWidthHere;
}

// Seeded into `noseLength` the first time an end is switched to Rounded. The bake's own fallback
// for an unset nose is the geometric one, a half-circle of `width / 2` -- correct, but only a
// couple of metres long on a reservation running hundreds, so a freshly-picked Rounded end would
// look identical to Mitred until the length was raised by hand. This starts it somewhere visible
// at the zoom people actually author at.
constexpr double kDefaultNoseLength = 40.0;

// One end's style combo, cap-width field, and (Rounded only) nose-length field. Returns true if
// `cap` was mutated; caller re-clamps via editReservation same as t0/t1/width above.
bool DrawEndCapControls(const char* label, ReservationEndCap& cap) {
  bool changed = false;
  int styleIndex = static_cast<int>(cap.style);
  ImGui::SetNextItemWidth(120);
  char comboId[64];
  std::snprintf(comboId, sizeof(comboId), "%s##style", label);
  if (ImGui::Combo(comboId, &styleIndex, kEndCapStyleNames, IM_ARRAYSIZE(kEndCapStyleNames))) {
    cap.style = static_cast<ReservationEndCapStyle>(styleIndex);
    if (cap.style == ReservationEndCapStyle::Rounded && cap.noseLength <= 0.0) cap.noseLength = kDefaultNoseLength;
    changed = true;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(cap.style == ReservationEndCapStyle::Joined);
  ImGui::SetNextItemWidth(100);
  char widthId[64];
  std::snprintf(widthId, sizeof(widthId), "Cap width##%s", label);
  bool fieldChanged = ImGui::InputDouble(widthId, &cap.width, 0.0, 0.0, "%.1f", kCommitOnEnter);
  fieldChanged |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::EndDisabled();

  ImGui::BeginDisabled(cap.style != ReservationEndCapStyle::Rounded);
  ImGui::SetNextItemWidth(100);
  char noseId[64];
  std::snprintf(noseId, sizeof(noseId), "Nose length (m)##%s", label);
  fieldChanged |= ImGui::InputDouble(noseId, &cap.noseLength, 0.0, 0.0, "%.1f", kCommitOnEnter);
  fieldChanged |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::EndDisabled();

  return changed || (fieldChanged && cap.style != ReservationEndCapStyle::Joined);
}

}  // namespace

bool DrawReservationsPanel(EditorState& state, int currentPathIndex, const tox::Track* baked) {
  bool mutated = false;
  const auto& selectedId = state.selectedReservationId();
  const Reservation* reservation = selectedId.has_value() ? state.findReservation(*selectedId) : nullptr;

  if (reservation != nullptr) {
    const std::string id = reservation->id;
    ImGui::Text("Reservation: %s", id.c_str());

    double t0 = reservation->t0 * 100.0, t1 = reservation->t1 * 100.0, width = reservation->width;
    double wallHeight = reservation->wallHeight;
    ReservationWidthMode widthMode = reservation->widthMode;
    ReservationEndCap endCap0 = reservation->endCap0, endCap1 = reservation->endCap1;
    bool changed = false;
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputDouble("t0 (%)", &t0, 0.0, 0.0, "%.1f", kCommitOnEnter);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::InputDouble("t1 (%)", &t1, 0.0, 0.0, "%.1f", kCommitOnEnter);
    changed |= ImGui::IsItemDeactivatedAfterEdit();

    // Road width at this reservation's own midpoint, used both to auto-convert `width` when the
    // mode toggle flips and to show the Percent field's resolved metres value alongside it.
    const auto located = state.locateReservation(id);
    const bool pathClosed = located.has_value() && located->first < static_cast<int>(state.track().paths.size())
                                ? state.track().paths[located->first].closed
                                : true;
    const double roadWidthHere = located.has_value() ? widthAtT(baked, located->first, pathClosed, (reservation->t0 + reservation->t1) / 2.0) : 0.0;

    ImGui::SetNextItemWidth(140);
    int modeIndex = static_cast<int>(widthMode);
    if (ImGui::Combo("Width mode", &modeIndex, kWidthModeNames, IM_ARRAYSIZE(kWidthModeNames))) {
      const auto newMode = static_cast<ReservationWidthMode>(modeIndex);
      ConvertWidthForMode(width, widthMode, newMode, roadWidthHere);
      widthMode = newMode;
      changed = true;
    }
    ImGui::SetNextItemWidth(120);
    if (widthMode == ReservationWidthMode::Percent) {
      // A slider rather than a typed number for Percent -- "a percentage of the road" reads more
      // naturally as a position on a 0-100 range you drag than as a value you type, unlike Fixed's
      // metres (an open-ended quantity a slider's fixed range wouldn't suit).
      constexpr double kPercentMin = 0.0, kPercentMax = 100.0;
      changed |= ImGui::SliderScalar("Width (%)", ImGuiDataType_Double, &width, &kPercentMin, &kPercentMax, "%.1f");
    } else {
      changed |= ImGui::InputDouble("Width (m)", &width, 0.0, 0.0, "%.1f", kCommitOnEnter);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (widthMode == ReservationWidthMode::Percent && roadWidthHere > 0.0)
      ImGui::Text("  ~%.1f m at this reservation's midpoint", width / 100.0 * roadWidthHere);
    ImGui::SetNextItemWidth(120);
    // <= 0 means "use the engine default" (TrackCore::DEFAULT_RAIL_HEIGHT) -- this is both the
    // wall's render height and its physical height (Ship.cpp reads the same MeshRegion::railHeight
    // a car can clear once above), not a purely cosmetic knob.
    changed |= ImGui::InputDouble("Wall height (0 = default)", &wallHeight, 0.0, 0.0, "%.1f", kCommitOnEnter);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    changed |= DrawEndCapControls("t0 end", endCap0);
    changed |= DrawEndCapControls("t1 end", endCap1);

    if (changed) {
      mutated |= state.editReservation(id, [&](Reservation& target) {
        target.t0 = t0 / 100.0;
        target.t1 = t1 / 100.0;
        target.width = width;
        target.widthMode = widthMode;
        target.wallHeight = wallHeight;
        target.endCap0 = endCap0;
        target.endCap1 = endCap1;
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
        if (r.widthMode == ReservationWidthMode::Percent)
          ImGui::Text("%.1f%%", r.width);
        else
          ImGui::Text("%.1f", r.width);
      }
    }
    ImGui::EndTable();
  }

  return mutated;
}

}  // namespace editor
