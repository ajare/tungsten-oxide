// CurvesPanel.hpp — curve selector, Delete Curve, Connect/join, and a read-only junctions/disjoint
// -seams list (EDITOR_PARITY_FIXES.md gap 5). Disjoint/reconnect itself lives on the selected
// point's own Position fields (PropertiesPanel.cpp's "Disjoint (hard seam)" checkbox), matching
// where web/editor.html puts it (#disjointChk in the point-properties panel, not a separate window).
// See EditorState.hpp's "Connect/join" and "Disjoint / reconnect" section comments for the scope
// this deliberately does NOT cover (joining onto an interior point; self-intersection overrides).
#pragma once

#include "EditorState.hpp"

namespace editor {

// Returns true if the track was mutated (caller should rebake).
bool DrawCurvesPanel(EditorState& state);

}  // namespace editor
