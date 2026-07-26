/* test/parity/autopilot.js — a crude, seeded "noisy autopilot": throwaway JS
 * scaffolding whose only job is to drive the ship somewhere interesting so the
 * golden trace exercises real physics (curved driving, wall bounces, and — on an
 * open curve — running off the end into the airborne/landing code). Its outputs
 * are baked into the trace as explicit per-step inputs, so the C++ engine just
 * replays them; the controller itself is never ported.
 *
 * Pure-random steering would just grind into the first wall and test almost
 * nothing (CPP_PORT_PLAN.md §5), so this follows the centerline: steer to align
 * the nose with the track tangent, nudged back toward the centerline, plus a
 * seeded perturbation and the occasional lift/brake.
 */

import { projectToSurface, signedAngleAbout, tangentize, clamp } from '../../js/track-physics.js';
import { Vec3 } from '../../js/vec3.js';

// mulberry32 — the exact seeded PRNG the editor's random generator uses.
export function mulberry32(a) {
  return function () {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function makeAutopilot(seed) {
  const rnd = mulberry32(seed >>> 0);
  const up0 = new Vec3(0, 1, 0);
  return function intent(sim, ship) {
    const p = ship.physics;
    const s = sim.sampleTrack(p.groundPos.x, p.groundPos.y, p.groundPos.z);
    const axis = p.airborne ? up0 : s.normal;

    // Tangent in the ship's current travel sense.
    const tgt = s.tangent.clone();
    if (tgt.dot(p.forward) < 0) tgt.multiplyScalar(-1);

    // Pull back toward the centerline: proj.s is the signed lateral offset along
    // edgeRight, so bias the target heading against it.
    const proj = projectToSurface(s, p.groundPos.x, p.groundPos.y, p.groundPos.z);
    const desired = tgt.clone().addScaledVector(s.edgeRight, -clamp(proj.s * 0.02, -1, 1));
    tangentize(desired, axis, tgt);

    const ang = signedAngleAbout(p.forward, desired, axis);
    const noise = (rnd() - 0.5) * 0.5;
    const steer = clamp(ang * 2.5 + noise, -1, 1);

    let throttle = 1, brake = 0;
    const r = rnd();
    if (r < 0.03) { brake = 1; throttle = 0; }        // occasional dab of brake
    else if (r < 0.09) { throttle = 0; }               // occasional coast
    return { throttle, brake, steer };
  };
}
