#include "MeshPanel.hpp"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

namespace editor {
namespace {

constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;

// There's no on-canvas list of placed mesh regions -- each one is picked by clicking it, which
// means a region hidden behind another (or off-screen after a pan) has no way back into selection.
// Mirrors the "Existing roll/width/cross-section points"/"Existing zones"/"Existing triggers"
// tables elsewhere in this UI (no JS precedent -- editor.js has no mesh list either, canvas-click
// is its only selection path too).
void drawMeshListTable(EditorState& state) {
  ImGui::Separator();
  ImGui::TextUnformatted("Existing mesh regions:");
  const auto& selectedId = state.selectedMeshId();
  constexpr ImGuiTableFlags kMeshTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("meshesTable", 6, kMeshTableFlags)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Asset");
    ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthFixed, 55.0f);
    ImGui::TableSetupColumn("Elevation", ImGuiTableColumnFlags_WidthFixed, 65.0f);
    ImGui::TableSetupColumn("Rotation", ImGuiTableColumnFlags_WidthFixed, 65.0f);
    ImGui::TableHeadersRow();
    for (const auto& m : state.track().meshes) {
      const bool isSelected = selectedId.has_value() && *selectedId == m.id;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      char rowId[80];
      std::snprintf(rowId, sizeof(rowId), "##mesh-%s", m.id.c_str());
      if (ImGui::Selectable(rowId, isSelected, ImGuiSelectableFlags_SpanAllColumns)) state.selectMesh(m.id);
      ImGui::SameLine();
      ImGui::TextUnformatted(m.id.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(m.assetId.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", m.x);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", m.z);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", m.elevation);
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", m.rotation);
    }
    ImGui::EndTable();
  }
}

}  // namespace

bool DrawMeshPanel(EditorState& state) {
  bool mutated = false;
  const auto& selectedId = state.selectedMeshId();
  const MeshPlacement* placement = selectedId.has_value() ? state.findMeshPlacement(*selectedId) : nullptr;

  if (placement == nullptr) {
    ImGui::TextUnformatted("No mesh region selected.");
    drawMeshListTable(state);
    return mutated;
  }

  const auto assetIt = state.track().meshAssets.find(placement->assetId);
  const MeshAsset* asset = assetIt != state.track().meshAssets.end() ? &assetIt->second : nullptr;

  ImGui::Text("Mesh region: %s", placement->id.c_str());
  ImGui::Text("Asset: %s", placement->assetId.c_str());

  double x = placement->x, z = placement->z, elevation = placement->elevation, rotation = placement->rotation;
  bool changed = false;
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("X", &x, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Z", &z, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Elevation", &elevation, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputDouble("Rotation (deg)", &rotation, 0.0, 0.0, "%.1f", kCommitOnEnter);
  changed |= ImGui::IsItemDeactivatedAfterEdit();

  if (changed) {
    mutated |= state.editMeshPlacement(placement->id, [&](MeshPlacement& target) {
      target.x = x;
      target.z = z;
      target.elevation = elevation;
      target.rotation = rotation;
    });
  }

  // Rail height is per-ASSET, not per-placement -- applies to every placement of this asset, same
  // as js/editor.js's title="Applies to every placement of this asset" (js/editor.js:2198).
  double railHeight = asset != nullptr ? asset->railHeight : 6.0;
  ImGui::SetNextItemWidth(120);
  bool railChanged = ImGui::InputDouble("Rail Height", &railHeight, 0.0, 0.0, "%.1f", kCommitOnEnter);
  railChanged |= ImGui::IsItemDeactivatedAfterEdit();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Applies to every placement of this asset");
  if (railChanged && state.setMeshAssetRailHeight(placement->assetId, railHeight)) mutated = true;

  const int edgeCount = asset != nullptr ? static_cast<int>(asset->edges.size()) : 0;
  const int railCount =
      asset != nullptr ? static_cast<int>(std::count_if(asset->edges.begin(), asset->edges.end(), [](const MeshEdge& e) { return e.rail; })) : 0;
  ImGui::TextWrapped(
      "%d of %d edges railed. Switch to Rails mode to click edges: railed edges are solid walls, bare edges are ledges you drive off.", railCount,
      edgeCount);

  if (ImGui::Button("Delete Mesh Region")) {
    if (state.deleteSelectedMesh()) mutated = true;
  }

  drawMeshListTable(state);
  return mutated;
}

}  // namespace editor
