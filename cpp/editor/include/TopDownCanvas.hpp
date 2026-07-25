// TopDownCanvas.hpp — the top-down 2D view (EDITOR_CPP_PORT_PLAN.md M2): draws the track's baked
// road surface/centerline and its authored position control points via ImDrawList inside an ImGui
// child window, and handles pan (right-drag) / zoom (scroll) input. No editing yet (M3+): this
// milestone only renders and navigates.
#pragma once

#include "EditorTrackDefinition.hpp"
#include "TopDownView.hpp"
#include "Track.hpp"

namespace editor {

// `baked` may be null (e.g. the current authoring state failed to bake) -- the control points
// and grid still render so the user isn't staring at a blank canvas mid-edit.
void DrawTopDownCanvas(TopDownView& view, const TrackDefinition& authored, const tox::Track* baked);

}  // namespace editor
