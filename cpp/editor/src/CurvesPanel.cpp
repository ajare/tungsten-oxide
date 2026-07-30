#include "CurvesPanel.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "imgui.h"

namespace editor {

bool DrawCurvesPanel(EditorState& state) {
  bool mutated = false;
  const auto& paths = state.track().paths;

  ImGui::TextUnformatted("Curve selector:");
  int currentIndex = state.currentPathIndex();
  if (!paths.empty()) {
    std::vector<std::string> labels(paths.size());
    std::vector<const char*> items(paths.size());
    for (int i = 0; i < static_cast<int>(paths.size()); ++i) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "Curve %d%s", i, paths[i].closed ? " (closed)" : "");
      labels[i] = buf;
      items[i] = labels[i].c_str();
    }
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("##curveSelect", &currentIndex, items.data(), static_cast<int>(items.size()))) state.setCurrentPathIndex(currentIndex);
  } else {
    ImGui::TextUnformatted("(no curves)");
  }

  ImGui::BeginDisabled(paths.size() <= 1);
  if (ImGui::Button("Delete Curve")) {
    if (state.deleteCurrentPath()) mutated = true;
  }
  ImGui::EndDisabled();

  // Delete outgoing/incoming segment: only shown for a selected Position point, and only the
  // button whose direction actually has a segment (an open path's last/first point has no
  // outgoing/incoming segment respectively). Grouped here with Delete Curve/Join as a
  // curve-topology edit, rather than in the point's own Properties fields. Labelled with the same
  // red/green the two segments are highlighted with on the top-down canvas (see TopDownCanvas.cpp's
  // kOutgoingSegmentColor/kIncomingSegmentColor).
  if (state.selection().valid()) {
    const auto outgoingSeg = state.selectedOutgoingSegment();
    const auto incomingSeg = state.selectedIncomingSegment();
    if (outgoingSeg.has_value() || incomingSeg.has_value()) {
      ImGui::Separator();
      ImGui::TextUnformatted("Selected point's segments:");
      if (outgoingSeg.has_value() && ImGui::Button("Delete Outgoing Segment (red)")) {
        if (state.deleteSelectedSegment(true)) mutated = true;
      }
      if (incomingSeg.has_value() && ImGui::Button("Delete Incoming Segment (green)")) {
        if (state.deleteSelectedSegment(false)) mutated = true;
      }
    }
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Connect two open curve endpoints:");
  // Only open paths are connectable -- closed paths have no endpoints (mirrors performJoin's
  // aEndpoint/bEndpoint checks, which only ever pass for the first/last point of an open path).
  std::vector<int> openPathIndices;
  for (int i = 0; i < static_cast<int>(paths.size()); ++i)
    if (!paths[i].closed) openPathIndices.push_back(i);

  static int aChoice = 0, bChoice = 0;  // indices into openPathIndices
  static int aEnd = 0, bEnd = 1;        // 0 = start, 1 = end
  const bool canConnect = openPathIndices.size() >= 1;
  if (canConnect) {
    std::vector<std::string> labels(openPathIndices.size());
    std::vector<const char*> items(openPathIndices.size());
    for (int i = 0; i < static_cast<int>(openPathIndices.size()); ++i) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "Curve %d", openPathIndices[i]);
      labels[i] = buf;
      items[i] = labels[i].c_str();
    }
    aChoice = std::min(aChoice, static_cast<int>(items.size()) - 1);
    bChoice = std::min(bChoice, static_cast<int>(items.size()) - 1);
    const char* endNames[] = {"Start", "End"};

    ImGui::SetNextItemWidth(140);
    ImGui::Combo("Curve A", &aChoice, items.data(), static_cast<int>(items.size()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::Combo("End A", &aEnd, endNames, 2);

    ImGui::SetNextItemWidth(140);
    ImGui::Combo("Curve B", &bChoice, items.data(), static_cast<int>(items.size()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::Combo("End B", &bEnd, endNames, 2);

    if (ImGui::Button("Join")) {
      if (state.joinPathEndpoints(openPathIndices[aChoice], aEnd == 1, openPathIndices[bChoice], bEnd == 1)) mutated = true;
    }
    ImGui::TextUnformatted("Same curve, both ends -> closes it. Different curves -> merges by sharing an endpoint.");
  } else {
    ImGui::TextUnformatted("(no open curves to connect)");
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Junctions (read-only; created by Join):");
  if (state.junctions().empty()) ImGui::TextUnformatted("(none)");
  constexpr ImGuiTableFlags kListTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;
  if (!state.junctions().empty() && ImGui::BeginTable("junctionsTable", 4, kListTableFlags)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Point");
    ImGui::TableSetupColumn("Source");
    ImGui::TableSetupColumn("Target");
    ImGui::TableHeadersRow();
    for (const auto& j : state.junctions()) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(j.id.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(j.pointId.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%s of %s", j.sourceEnd.c_str(), j.sourcePathId.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(j.targetPathId.c_str());
    }
    ImGui::EndTable();
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Disjoint seams (edit from the seamed point's own properties):");
  if (state.disjointSeams().empty()) ImGui::TextUnformatted("(none)");
  // Collects the clicked seam id rather than calling reconnectDisjoint() mid-loop: it erases from
  // the very vector this range-based for is iterating (state.disjointSeams()), which would
  // invalidate the loop's iterator.
  std::string pendingReconnectId;
  if (!state.disjointSeams().empty() && ImGui::BeginTable("disjointSeamsTable", 5, kListTableFlags)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Kind");
    ImGui::TableSetupColumn("Point");
    ImGui::TableSetupColumn("Path(s)");
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableHeadersRow();
    for (const auto& s : state.disjointSeams()) {
      const bool openedClosed = s.kind == "opened-closed";
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(s.id.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(openedClosed ? "opened-closed" : "split-open");
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(s.pointId.c_str());
      ImGui::TableNextColumn();
      if (openedClosed)
        ImGui::TextUnformatted(s.pathId.c_str());
      else
        ImGui::Text("%s / %s", s.leftPathId.c_str(), s.rightPathId.c_str());
      ImGui::TableNextColumn();
      char buf[32];
      std::snprintf(buf, sizeof(buf), "Reconnect##%s", s.id.c_str());
      if (ImGui::Button(buf)) pendingReconnectId = s.id;
    }
    ImGui::EndTable();
  }
  if (!pendingReconnectId.empty() && state.reconnectDisjoint(pendingReconnectId)) mutated = true;

  return mutated;
}

}  // namespace editor
