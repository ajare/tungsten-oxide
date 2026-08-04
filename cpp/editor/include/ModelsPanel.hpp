// ModelsPanel.hpp — lists every embedded <Model> (TRACK_MODEL_LIST_PLAN.md Milestone 6's "Load
// Model") in the current Track, alongside how many placements reference it and its own mesh count.
// Clicking a row selects one of its placements (via EditorState::selectMeshObject), so the existing
// Properties panel's per-mesh Type/Visible editor (PropertiesPanel.cpp's drawMeshObjectFields) is
// the one place that metadata is actually edited -- this panel is a browse/select surface, not a
// second editor for the same data.
#pragma once

#include "EditorState.hpp"

namespace editor {

void DrawModelsPanel(EditorState& state);

}  // namespace editor
