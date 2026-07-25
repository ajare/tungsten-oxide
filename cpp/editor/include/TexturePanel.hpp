// TexturePanel.hpp — texture-asset library UI (EDITOR_CPP_PORT_PLAN.md M7b): mirrors editor.js's
// texture panel (renderTexturePanel/textureGrid), but as an ImGui panel using real GL textures
// (TextureCache) for the tile-grid thumbnails instead of CSS background-position slicing.
#pragma once

#include "EditorState.hpp"
#include "TextureCache.hpp"

namespace editor {

// Draws the texture asset list + tile-grid picker, assigning/clearing `currentPathIndex`'s
// texture binding on click (mirrors currentCurve()'s role in editor.js: whichever path is
// selected, or the first path). Returns true if the track was mutated (caller should rebake).
bool DrawTexturePanel(EditorState& state, TextureCache& textures, int currentPathIndex);

// Scans assets/track/manifest.json (via findAssetsDir()) and registers any texture not already
// present as a texture asset -- mirrors loadBundledTextureAssets(), minus the fetch() (this reads
// straight off disk, since there's no browser/HTTP server here). Returns the number of newly
// added assets.
int loadBundledTextureAssets(EditorState& state);

}  // namespace editor
