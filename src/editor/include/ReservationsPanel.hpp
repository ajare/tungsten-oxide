// ReservationsPanel.hpp — central-reservation list + t0/t1/width fields (CENTRAL_RESERVATION_PLAN.md
// M3). Panel-only authoring, matching the current
// state of roll/width/cross-section points: no on-canvas click-to-place or drag.
#pragma once

#include "EditorState.hpp"
#include "Track.hpp"

namespace editor {

// `currentPathIndex` is where a freshly added reservation lands -- same convention as
// ZonesPanel.cpp/TriggersPanel.cpp/PropertiesPanel.cpp. `baked` (may be null) lets the panel show
// the resolved metres value next to a Percent-mode width, via PropertiesPanel.hpp's widthAtT --
// same baked track TriggersPanel.cpp already samples for its own auto-width preview. Returns true
// if the track was mutated (caller should rebake).
bool DrawReservationsPanel(EditorState& state, int currentPathIndex, const tox::Track* baked);

}  // namespace editor
