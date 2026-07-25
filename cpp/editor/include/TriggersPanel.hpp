// TriggersPanel.hpp — trigger (dummy / checkpoint) list + fields (EDITOR_PARITY_FIXES.md gap 4),
// mirroring js/editor.js's renderProps() trigger branch and its right-click "Add trigger" menu.
// See EditorState.hpp's "Triggers" section for the scope this deliberately does NOT cover
// (on-canvas drag; creating a mesh-hosted trigger from scratch).
#pragma once

#include "EditorState.hpp"

namespace editor {

// `currentPathIndex` is where a freshly added path trigger lands -- same convention as
// ZonesPanel.cpp/PropertiesPanel.cpp. Returns true if the track was mutated (caller should
// rebake).
bool DrawTriggersPanel(EditorState& state, int currentPathIndex);

}  // namespace editor
