// MaterialsPanel.hpp — replaces the old texture-asset library UI (TexturePanel.hpp) with a
// read-only list of the TrackMaterials MaterialCatalog loaded from Resources.yaml at startup,
// letting the user assign one to the currently-selected curve. Unlike TexturePanel, there is no
// authoring here (add/browse/tile-size). The catalog can be refreshed from editor.ini's Resources
// XML while the process is running; unresolved assignments remain visible until then.
#pragma once

#include "EditorState.hpp"
#include "MaterialCatalog.hpp"
#include "TextureCache.hpp"

namespace editor {

// Draws the TrackMaterial list (name + first texture's thumbnail via TextureCache), assigning
// `currentPathIndex`'s material on click, current assignment highlighted. Returns true if the
// track was mutated (caller should rebake).
bool DrawMaterialsPanel(EditorState& state, const MaterialCatalog& materialCatalog, TextureCache& textures, int currentPathIndex);

}  // namespace editor
