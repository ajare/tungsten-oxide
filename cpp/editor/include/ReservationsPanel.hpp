// ReservationsPanel.hpp — central-reservation list + t0/t1/width fields (CENTRAL_RESERVATION_PLAN.md
// M3). Panel-only authoring, matching the current
// state of roll/width/cross-section points: no on-canvas click-to-place or drag.
#pragma once

#include "EditorState.hpp"

namespace editor {

// `currentPathIndex` is where a freshly added reservation lands -- same convention as
// ZonesPanel.cpp/TriggersPanel.cpp/PropertiesPanel.cpp. Returns true if the track was mutated
// (caller should rebake).
bool DrawReservationsPanel(EditorState& state, int currentPathIndex);

}  // namespace editor
