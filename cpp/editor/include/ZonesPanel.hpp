// ZonesPanel.hpp — zone (boost pad / start grid) list + fields (EDITOR_PARITY_FIXES.md gap 3),
// mirroring web/js/editor.js's renderProps() zone branch and its right-click "Add Zone" menu. See
// EditorState.hpp's "Zones" section for the scope this deliberately does NOT cover (on-canvas
// drag; creating a mesh-hosted zone from scratch).
#pragma once

#include "EditorState.hpp"

namespace editor {

// `currentPathIndex` is where a freshly added path zone lands -- same convention as
// PropertiesPanel.cpp/TexturePanel.cpp. Returns true if the track was mutated (caller should
// rebake).
bool DrawZonesPanel(EditorState& state, int currentPathIndex);

}  // namespace editor
