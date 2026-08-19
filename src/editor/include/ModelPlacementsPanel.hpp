// ModelPlacementsPanel.hpp — the selected Model placement's fields (position/rotation/scale, the
// referenced embedded Model's own per-mesh Type/Visible metadata) plus a table of every placement
// on the Track, mirroring ZonesPanel.cpp's "selected item's fields + table of all items" shape. A
// separate left-panel section from Point Properties (which only ever concerns itself with path
// control points) and from ModelsPanel.hpp (which lists embedded <Model> definitions, not their
// placed instances).
#pragma once

#include "EditorState.hpp"

namespace editor {

// Returns true if the track was mutated (caller should rebake).
bool DrawModelPlacementsPanel(EditorState& state);

}  // namespace editor
