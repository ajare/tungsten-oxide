// ElevationView.hpp — the elevation profile side view (EDITOR_CPP_PORT_PLAN.md M6), a second
// ImDrawList canvas alongside the top-down view: x is position along the current path, y is world
// elevation. Mirrors web/js/editor.js's elevCanvas/drawElev/dragging === 'elev'.
//
// The x-axis is true cumulative XZ arc length along the baked centerline (EDITOR_PARITY_GAPS.md
// gap 9), matching web/js/editor.js's own arc-length axis (web/js/editor.js:1433-1451) -- each control
// point's screen x comes from measuring the baked curve, not its authored order, so the profile
// line and the point handles plotted on it line up exactly even after reordering or uneven
// spacing. Falls back to plain order-based spacing only when there's no baked centerline yet to
// measure (mid-edit bake failure), matching this file's existing "baked may be null" tolerance.
#pragma once

#include "EditorState.hpp"
#include "Track.hpp"

namespace editor {

// `baked` may be null; the point markers still render so mid-edit state (e.g. a bake failure)
// doesn't blank the panel. `pathIndex` selects which path's profile to show (typically the
// current selection's path, or 0). `showPositionPoints` mirrors the top-down view's own Position
// point-filter checkbox (TopDownView::showPositionPoints) -- gates right-click-to-insert the same
// way JS's `pointFilters.position` does (web/js/editor.js:3557), so hiding position handles in one
// view hides the ability to add them in both. Returns true if the track was mutated this frame
// (an elevation drag, or a right-click insert), so the caller knows to re-bake the live preview.
bool DrawElevationView(EditorState& state, const tox::Track* baked, int pathIndex, bool showPositionPoints);

}  // namespace editor
