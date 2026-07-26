// TriggersPanel.hpp — trigger (dummy / checkpoint) list + fields (EDITOR_PARITY_FIXES.md gap 4),
// mirroring web/js/editor.js's renderProps() trigger branch and its right-click "Add trigger" menu.
// See EditorState.hpp's "Triggers" section for the scope this deliberately does NOT cover
// (creating a mesh-hosted trigger from scratch).
#pragma once

#include "EditorState.hpp"
#include "Track.hpp"

namespace editor {

// `currentPathIndex` is where a freshly added path trigger lands -- same convention as
// ZonesPanel.cpp/PropertiesPanel.cpp. `baked` backs the auto-width feature (resolving a
// path-hosted trigger's road width at its host t via widthAtT, PropertiesPanel.hpp). Returns true
// if the track was mutated (caller should rebake).
bool DrawTriggersPanel(EditorState& state, int currentPathIndex, const tox::Track* baked);

}  // namespace editor
