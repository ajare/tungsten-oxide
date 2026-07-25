// ElevationView.hpp — the elevation profile side view (EDITOR_CPP_PORT_PLAN.md M6), a second
// ImDrawList canvas alongside the top-down view: x is position along the current path, y is world
// elevation. Mirrors js/editor.js's elevCanvas/drawElev/dragging === 'elev'.
//
// Simplification vs editor.js: the x-axis there places each authored point at its true
// spline-parametrized arc-length position (evaluated against the baked curve). This view instead
// spaces position points evenly by their authored ORDER within the path. Order and true parameter
// position agree for any path that hasn't had points reordered independently of their spline
// placement (true for every path this editor can currently produce -- Create mode appends in
// click order, and nothing here can reorder points out of sequence), so this is a faithful
// simplification for what the editor can build today, not a permanent shortcut; a later milestone
// can switch to true arc-length placement (matching the baked centerline profile drawn alongside
// it) without changing the interaction model.
#pragma once

#include "EditorState.hpp"
#include "Track.hpp"

namespace editor {

// `baked` may be null; the point markers still render so mid-edit state (e.g. a bake failure)
// doesn't blank the panel. `pathIndex` selects which path's profile to show (typically the
// current selection's path, or 0). Returns true if the track was mutated this frame (an elevation
// drag), so the caller knows to re-bake the live preview.
bool DrawElevationView(EditorState& state, const tox::Track* baked, int pathIndex);

}  // namespace editor
