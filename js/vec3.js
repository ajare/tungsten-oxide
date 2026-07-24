/* vec3.js — a tiny hand-rolled 3D vector, a byte-for-byte behavioural mirror of
 * THREE.Vector3 *as shipped in three.js r128* (the exact CDN build track.html
 * loads). It exists so the physics core (js/track-physics.js) can be pulled out
 * of the THREE-dependent game and run headless in Node — and so the eventual
 * C++ port has a single, unambiguous vector semantics to transliterate.
 *
 * PARITY IS THE WHOLE POINT. Do NOT "modernize" these method bodies:
 *
 *  - `normalize()` divides by `length() || 1`, so a zero-length vector stays the
 *    zero vector instead of becoming NaN. track-physics relies on this edge case
 *    (see tangentize / beginAirborne).
 *  - `applyQuaternion()` uses r128's inverse-quaternion form (ix/iy/iz/iw), NOT
 *    the `t = 2 * cross(q.xyz, v)` form three.js switched to around r150. The two
 *    are algebraically equal but differ in the last ULP because float add is not
 *    associative — matching r128's *operation order* is what keeps per-step
 *    parity with the shipping game (and, later, with the C++ engine).
 *  - `applyAxisAngle()` goes through setFromAxisAngle → applyQuaternion, exactly
 *    as THREE does; it is NOT a direct Rodrigues rotation.
 *
 * Only the subset of Vector3's surface the track code actually uses is provided.
 * Every method that mutates returns `this`, matching THREE's chaining contract,
 * so call sites transliterate unchanged.
 */

// Module-scratch quaternion, mirroring THREE's internal `_quaternion` reused by
// Vector3.applyAxisAngle so no allocation happens per rotation.
const _q = { x: 0, y: 0, z: 0, w: 1 };

// Quaternion.setFromAxisAngle (r128). `axis` is assumed to be a unit vector.
function setQuatFromAxisAngle(q, axis, angle) {
  const halfAngle = angle / 2, s = Math.sin(halfAngle);
  q.x = axis.x * s;
  q.y = axis.y * s;
  q.z = axis.z * s;
  q.w = Math.cos(halfAngle);
  return q;
}

export class Vec3 {
  constructor(x = 0, y = 0, z = 0) {
    this.x = x;
    this.y = y;
    this.z = z;
  }

  set(x, y, z) {
    this.x = x;
    this.y = y;
    this.z = z;
    return this;
  }

  clone() {
    return new Vec3(this.x, this.y, this.z);
  }

  copy(v) {
    this.x = v.x;
    this.y = v.y;
    this.z = v.z;
    return this;
  }

  add(v) {
    this.x += v.x;
    this.y += v.y;
    this.z += v.z;
    return this;
  }

  addVectors(a, b) {
    this.x = a.x + b.x;
    this.y = a.y + b.y;
    this.z = a.z + b.z;
    return this;
  }

  addScaledVector(v, s) {
    this.x += v.x * s;
    this.y += v.y * s;
    this.z += v.z * s;
    return this;
  }

  sub(v) {
    this.x -= v.x;
    this.y -= v.y;
    this.z -= v.z;
    return this;
  }

  subVectors(a, b) {
    this.x = a.x - b.x;
    this.y = a.y - b.y;
    this.z = a.z - b.z;
    return this;
  }

  multiply(v) {
    this.x *= v.x;
    this.y *= v.y;
    this.z *= v.z;
    return this;
  }

  multiplyScalar(scalar) {
    this.x *= scalar;
    this.y *= scalar;
    this.z *= scalar;
    return this;
  }

  divideScalar(scalar) {
    return this.multiplyScalar(1 / scalar);
  }

  // Rotate about a (unit) axis by `angle` radians, via a quaternion — THREE's
  // exact path, not a direct Rodrigues formula.
  applyAxisAngle(axis, angle) {
    return this.applyQuaternion(setQuatFromAxisAngle(_q, axis, angle));
  }

  // Vector3.applyQuaternion (r128). Kept in this exact operation order on
  // purpose — see the file header.
  applyQuaternion(q) {
    const x = this.x, y = this.y, z = this.z;
    const qx = q.x, qy = q.y, qz = q.z, qw = q.w;

    // calculate quat * vector
    const ix = qw * x + qy * z - qz * y;
    const iy = qw * y + qz * x - qx * z;
    const iz = qw * z + qx * y - qy * x;
    const iw = -qx * x - qy * y - qz * z;

    // calculate result * inverse quat
    this.x = ix * qw + iw * -qx + iy * -qz - iz * -qy;
    this.y = iy * qw + iw * -qy + iz * -qx - ix * -qz;
    this.z = iz * qw + iw * -qz + ix * -qy - iy * -qx;
    return this;
  }

  dot(v) {
    return this.x * v.x + this.y * v.y + this.z * v.z;
  }

  cross(v) {
    return this.crossVectors(this, v);
  }

  crossVectors(a, b) {
    const ax = a.x, ay = a.y, az = a.z;
    const bx = b.x, by = b.y, bz = b.z;
    this.x = ay * bz - az * by;
    this.y = az * bx - ax * bz;
    this.z = ax * by - ay * bx;
    return this;
  }

  lengthSq() {
    return this.x * this.x + this.y * this.y + this.z * this.z;
  }

  length() {
    return Math.sqrt(this.x * this.x + this.y * this.y + this.z * this.z);
  }

  // Zero-length → divideScalar(1): stays the zero vector, never NaN.
  normalize() {
    return this.divideScalar(this.length() || 1);
  }

  setLength(length) {
    return this.normalize().multiplyScalar(length);
  }

  lerp(v, alpha) {
    this.x += (v.x - this.x) * alpha;
    this.y += (v.y - this.y) * alpha;
    this.z += (v.z - this.z) * alpha;
    return this;
  }

  distanceTo(v) {
    return Math.sqrt(this.distanceToSquared(v));
  }

  distanceToSquared(v) {
    const dx = this.x - v.x, dy = this.y - v.y, dz = this.z - v.z;
    return dx * dx + dy * dy + dz * dz;
  }

  negate() {
    this.x = -this.x;
    this.y = -this.y;
    this.z = -this.z;
    return this;
  }

  equals(v) {
    return v.x === this.x && v.y === this.y && v.z === this.z;
  }

  fromArray(array, offset = 0) {
    this.x = array[offset];
    this.y = array[offset + 1];
    this.z = array[offset + 2];
    return this;
  }

  toArray(array = [], offset = 0) {
    array[offset] = this.x;
    array[offset + 1] = this.y;
    array[offset + 2] = this.z;
    return array;
  }
}

export default Vec3;
