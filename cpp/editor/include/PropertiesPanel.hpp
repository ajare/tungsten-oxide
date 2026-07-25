// PropertiesPanel.hpp — the selected control point's editable fields (EDITOR_PARITY_FIXES.md gap
// 1), mirroring js/editor.js's renderProps() Selected Point panel: typed numeric inputs per point
// kind plus Delete, and (new here, since there's no on-canvas handle to click) an explicit
// path/t/Add flow for roll/width/crossSection points. See EditorState.hpp's "Roll/width/
// cross-section point editing" section for the scope this deliberately does NOT cover.
#pragma once

#include "EditorState.hpp"

namespace editor {

// `currentPathIndex` is where a freshly Added roll/width/crossSection point lands -- mirrors
// currentCurve()'s role elsewhere (TexturePanel.cpp, main.cpp): the selected point's path, or the
// first path if nothing's selected. Returns true if the track was mutated (caller should rebake).
bool DrawPropertiesPanel(EditorState& state, int currentPathIndex);

}  // namespace editor
