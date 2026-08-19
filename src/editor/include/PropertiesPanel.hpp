// PropertiesPanel.hpp — the selected control point's editable fields: typed numeric inputs per
// point kind plus Delete, and (since there's no on-canvas handle to click) an explicit path/t/Add
// flow for roll/width/crossSection points. See EditorState.hpp's "Roll/width/cross-section point
// editing" section for the scope this deliberately does NOT cover.
#pragma once

#include "EditorState.hpp"
#include "Track.hpp"
#include "TopDownView.hpp"

namespace editor {

// Road-surface width at a point's t, sampled (linearly interpolated) from the baked centerline --
// close enough for a preview (exact spline evaluation would need the authored width-point list,
// which callers don't otherwise touch). Shared with TriggersPanel.cpp (auto-width) as well as
// PropertiesPanel.cpp's own cross-section preview.
double widthAtT(const tox::Track* baked, int pathIndex, bool closed, double t);

// `currentPathIndex` is where a freshly Added roll/width/crossSection point lands -- mirrors
// currentCurve()'s role elsewhere (TexturePanel.cpp, main.cpp): the selected point's path, or the
// first path if nothing's selected. `view`/`baked` back the read-only physics-sample info section:
// when a physics sample is selected, it takes over the whole panel body. Returns true if the
// track was mutated (caller should rebake) -- always false while a physics sample is shown, since
// that section is read-only.
bool DrawPropertiesPanel(EditorState& state, int currentPathIndex, const TopDownView& view, const tox::Track* baked);

}  // namespace editor
