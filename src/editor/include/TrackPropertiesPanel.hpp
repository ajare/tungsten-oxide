// TrackPropertiesPanel.hpp — track-wide identity fields: name and start direction, in the docked
// "Panels" window as its own section so every property lives in one place.
#pragma once

#include "EditorState.hpp"

namespace editor {

// Returns true if the track was mutated (caller should rebake).
bool DrawTrackPropertiesPanel(EditorState& state);

}  // namespace editor
