#include "TrackPropertiesPanel.hpp"

#include <cstdio>

#include "imgui.h"

namespace editor {

bool DrawTrackPropertiesPanel(EditorState& state) {
  bool mutated = false;

  // Track name field. The buffer only resyncs from state.track().name when that value has
  // actually changed since last frame
  // (undo/redo/New/Import all go through setTrackName or replaceTrack, not live typing) --
  // otherwise a resync every frame would stomp in-progress keystrokes before they're committed.
  static char nameBuf[256] = "";
  static std::string lastSyncedName;
  if (lastSyncedName != state.track().name) {
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", state.track().name.c_str());
    lastSyncedName = state.track().name;
  }
  ImGui::SetNextItemWidth(220);
  ImGui::InputText("Track Name", nameBuf, sizeof(nameBuf));
  if (ImGui::IsItemDeactivatedAfterEdit() && state.setTrackName(nameBuf)) {
    lastSyncedName = state.track().name;
    mutated = true;
  }

  // Direction toggle.
  if (ImGui::Button(state.track().start.reverse ? "Direction: Reversed" : "Direction: Forward")) {
    state.toggleStartReverse();
    mutated = true;
  }

  return mutated;
}

}  // namespace editor
