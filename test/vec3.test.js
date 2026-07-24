import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Vec3 } from '../js/vec3.js';

const approx = (a, b, eps = 1e-12) => Math.abs(a - b) <= eps;
function assertVec(v, x, y, z, eps = 1e-12) {
  assert.ok(approx(v.x, x, eps) && approx(v.y, y, eps) && approx(v.z, z, eps),
    `expected (${x}, ${y}, ${z}) got (${v.x}, ${v.y}, ${v.z})`);
}

test('construction, copy, clone are independent', () => {
  const a = new Vec3(1, 2, 3);
  const b = a.clone();
  b.x = 9;
  assert.equal(a.x, 1);
  assertVec(b, 9, 2, 3);
  const c = new Vec3().copy(a);
  assertVec(c, 1, 2, 3);
});

test('arithmetic matches THREE op order', () => {
  const a = new Vec3(1, 2, 3);
  a.add(new Vec3(1, 1, 1));
  assertVec(a, 2, 3, 4);
  a.addScaledVector(new Vec3(1, 0, -1), 3);
  assertVec(a, 5, 3, 1);
  a.sub(new Vec3(5, 3, 1));
  assertVec(a, 0, 0, 0);
  const b = new Vec3().subVectors(new Vec3(4, 4, 4), new Vec3(1, 2, 3));
  assertVec(b, 3, 2, 1);
  b.multiplyScalar(2);
  assertVec(b, 6, 4, 2);
});

test('dot / cross / length', () => {
  assert.equal(new Vec3(1, 2, 3).dot(new Vec3(4, 5, 6)), 32);
  const c = new Vec3().crossVectors(new Vec3(1, 0, 0), new Vec3(0, 1, 0));
  assertVec(c, 0, 0, 1);
  assert.equal(new Vec3(3, 4, 0).length(), 5);
  assert.equal(new Vec3(3, 4, 0).lengthSq(), 25);
});

test('normalize on a zero vector stays zero (never NaN)', () => {
  const z = new Vec3(0, 0, 0).normalize();
  assertVec(z, 0, 0, 0);
  assert.ok(!Number.isNaN(z.x) && !Number.isNaN(z.y) && !Number.isNaN(z.z));
  const u = new Vec3(0, 3, 0).normalize();
  assertVec(u, 0, 1, 0);
});

test('lerp / distance', () => {
  const a = new Vec3(0, 0, 0).lerp(new Vec3(10, 0, 0), 0.25);
  assertVec(a, 2.5, 0, 0);
  assert.equal(new Vec3(0, 0, 0).distanceTo(new Vec3(0, 3, 4)), 5);
  assert.equal(new Vec3(0, 0, 0).distanceToSquared(new Vec3(0, 3, 4)), 25);
});

test('applyAxisAngle rotates +X about +Y by 90deg to -Z', () => {
  const v = new Vec3(1, 0, 0).applyAxisAngle(new Vec3(0, 1, 0), Math.PI / 2);
  assertVec(v, 0, 0, -1, 1e-12);
});

test('applyAxisAngle rotates +X about +Z by 90deg to +Y', () => {
  const v = new Vec3(1, 0, 0).applyAxisAngle(new Vec3(0, 0, 1), Math.PI / 2);
  assertVec(v, 0, 1, 0, 1e-12);
});

test('applyAxisAngle is a pure rotation (preserves length, full turn is identity)', () => {
  const axis = new Vec3(0.3, 0.5, -0.2).normalize();
  const v = new Vec3(2, -1, 4);
  const rotated = v.clone().applyAxisAngle(axis, 0.9);
  assert.ok(approx(rotated.length(), v.length(), 1e-12));
  const full = v.clone().applyAxisAngle(axis, Math.PI * 2);
  assertVec(full, v.x, v.y, v.z, 1e-12);
});
