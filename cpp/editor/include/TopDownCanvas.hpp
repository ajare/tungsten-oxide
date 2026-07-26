// TopDownCanvas.hpp — the top-down 2D view (EDITOR_CPP_PORT_PLAN.md M2): draws the track's baked
// road surface/centerline and its authored position control points via ImDrawList inside an ImGui
// child window, and handles pan (right-drag) / zoom (scroll) input. No editing yet (M3+): this
// milestone only renders and navigates.
#pragma once

#include "EditorState.hpp"
#include "TopDownView.hpp"
#include "Track.hpp"

namespace editor {

// `baked` may be null (e.g. the current authoring state failed to bake) -- the control points
// and grid still render so the user isn't staring at a blank canvas mid-edit. Mutates `state`
// in response to input: point select/drag/delete in Edit mode, click-to-add/close/finish in
// Create mode (EDITOR_CPP_PORT_PLAN.md M3). Returns true if the authored track changed this frame
// (the caller should re-bake the live preview).
bool DrawTopDownCanvas(TopDownView& view, EditorState& state, const tox::Track* baked);

// Pans+zooms `view` to frame whichever of the four mutually-exclusive selection kinds (point/
// mesh-region/zone/trigger) is currently selected -- shared by the top-down canvas's own "Object"
// zoom-slider button and main.cpp's 'x' hotkey/View-menu entry, so both go through the same
// bounds-resolution logic. No-op (returns false) if nothing is selected or the selection doesn't
// resolve to anything (stale, or `baked` not ready).
bool FocusOnSelection(TopDownView& view, const EditorState& state, const tox::Track* baked);

}  // namespace editor
