import test from 'node:test';
import assert from 'node:assert/strict';
import { DEFAULT_SHIP_COUNT, gridSlot, gridSlots } from '../js/ship-grid.js';

test('default grid is an alternating staggered 2 by 4 layout', () => {
  assert.deepEqual(gridSlots(), [
    { row: 0, column: 0, lateral: -2.5, behind: 0 },
    { row: 0, column: 1, lateral: 2.5, behind: 3 },
    { row: 1, column: 0, lateral: -2.5, behind: 11 },
    { row: 1, column: 1, lateral: 2.5, behind: 8 },
    { row: 2, column: 0, lateral: -2.5, behind: 16 },
    { row: 2, column: 1, lateral: 2.5, behind: 19 },
    { row: 3, column: 0, lateral: -2.5, behind: 27 },
    { row: 3, column: 1, lateral: 2.5, behind: 24 }
  ]);
  assert.equal(DEFAULT_SHIP_COUNT, 8);
});

test('grid expands deterministically beyond eight ships', () => {
  assert.deepEqual(gridSlot(8), { row: 4, column: 0, lateral: -2.5, behind: 32 });
  assert.deepEqual(gridSlot(9), { row: 4, column: 1, lateral: 2.5, behind: 35 });
});

test('narrow roads compress both columns symmetrically', () => {
  assert.equal(gridSlot(0, { lateralLimit: 1.25 }).lateral, -1.25);
  assert.equal(gridSlot(1, { lateralLimit: 1.25 }).lateral, 1.25);
  assert.equal(gridSlot(0, { lateralLimit: -2 }).lateral, 0);
  assert.equal(gridSlot(1, { lateralLimit: -2 }).lateral, 0);
});
