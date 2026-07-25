// RandomRangesPanel.hpp — configurable bounds for the random-track generator
// (EDITOR_PARITY_FIXES.md gap 8), mirroring js/editor.js's #randomPanel. `RandomTrack.hpp`'s
// generateRandomTrack already accepted a RandomTrackRanges parameter (M7a/M7c); this is the UI to
// edit one, which main.cpp previously never exposed (always passing the `{}` default).
//
// Unlike EditorState's own fields, these ranges are a generator *preference*, not authored track
// data -- they don't round-trip through the track JSON, aren't undoable, and (unlike JS's
// localStorage persistence) reset to RANDOM_RANGE_DEFAULTS every session, since this editor has no
// established local-settings-persistence mechanism yet (see TopDownView's grid/snap prefs for the
// same session-only tradeoff, gap 9).
#pragma once

#include "RandomTrack.hpp"

namespace editor {

// Draws every field pair plus a Reset-to-default button; sanitizes (clamps + fixes ordering,
// mirroring sanitizeRandomRanges) on every edit. Returns true if `ranges` changed.
bool DrawRandomRangesPanel(RandomTrackRanges& ranges);

}  // namespace editor
