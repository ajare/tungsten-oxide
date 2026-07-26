// MeshPanel.hpp — selected mesh region field editor (EDITOR_PARITY_GAPS.md gap 3), mirroring
// web/js/editor.js's renderProps() mesh-placement branch (web/js/editor.js:2178-2223): X/Z/elevation/
// rotation on the placement, rail height on the shared asset (affects every placement of it), a
// railed-edge count hint, and a delete button. On-canvas select/drag/shift-rotate/Delete-key and
// Rails-mode edge toggling already exist in EditorState/TopDownCanvas.cpp; this panel is the
// keyboard-editable counterpart for the fields those gestures can't reach (elevation, rail height).
#pragma once

#include "EditorState.hpp"

namespace editor {

// Returns true if the track was mutated (caller should rebake).
bool DrawMeshPanel(EditorState& state);

}  // namespace editor
