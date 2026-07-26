// Pure racing-grid layout. Longitudinal offsets are positive distances behind
// the authored start in the driven direction; lateral offsets are negative on
// the driver's left and positive on the right.
export const DEFAULT_SHIP_COUNT = 8;
export const GRID_COLUMNS = 2;
export const GRID_LATERAL_SPACING = 5;
export const GRID_ROW_SPACING = 8;
export const GRID_STAGGER = 3;

export function gridSlot(index, options = {}) {
  const lateralSpacing = options.lateralSpacing ?? GRID_LATERAL_SPACING;
  const lateralLimit = Math.max(0, options.lateralLimit ?? Infinity);
  const rowSpacing = options.rowSpacing ?? GRID_ROW_SPACING;
  const stagger = options.stagger ?? GRID_STAGGER;
  const row = Math.floor(index / GRID_COLUMNS);
  const column = index % GRID_COLUMNS; // 0 left, 1 right
  const leftIsAhead = row % 2 === 0;
  const isAhead = column === (leftIsAhead ? 0 : 1);
  const halfSpacing = Math.min(lateralSpacing / 2, lateralLimit);
  return {
    row,
    column,
    lateral: halfSpacing === 0 ? 0 : (column === 0 ? -halfSpacing : halfSpacing),
    behind: row * rowSpacing + (isAhead ? 0 : stagger)
  };
}

export function gridSlots(count = DEFAULT_SHIP_COUNT, options = {}) {
  return Array.from({ length: Math.max(0, count) }, (_, i) => gridSlot(i, options));
}
