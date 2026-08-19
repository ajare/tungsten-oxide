// HandlingPanel.hpp — ship handling fields: maxSpeed/accel/turnSpeed/weight, plus a
// Reset-to-default button.
#pragma once

#include "EditorState.hpp"

namespace editor {

// Returns true if the track was mutated (caller should rebake).
bool DrawHandlingPanel(EditorState& state);

}  // namespace editor
