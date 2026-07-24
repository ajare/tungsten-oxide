/* Regenerate the committed golden traces in test/traces/. Deliberate, reviewable
 * regeneration only — run when the physics is intentionally changed:
 *
 *   node test/parity/gen-traces.mjs
 *
 * The traces are committed fixtures read by BOTH engines (the JS self-check and
 * the C++ parity replayer), so they are the durable regression oracle even after
 * the JS is retired (CPP_PORT_PLAN.md §6). */

import { writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { installTrackCore } from './loadcore.js';

installTrackCore();
const { buildTrace } = await import('./trace.js');
const { tracks } = await import('./tracks.js');

const outDir = fileURLToPath(new URL('../traces/', import.meta.url));
mkdirSync(outDir, { recursive: true });

const manifest = [];
for (const { name, track, steps, seed } of tracks()) {
  const trace = buildTrace(track, { name, steps, seed });
  const file = `${name}.json`;
  writeFileSync(outDir + file, JSON.stringify(trace) + '\n');
  const airborneSteps = trace.steps.filter(s => s.after.physics.airborne).length;
  manifest.push({ file, steps: trace.steps.length, airborneSteps });
  console.log(`wrote ${file}: ${trace.steps.length} steps, ${airborneSteps} airborne`);
}
writeFileSync(outDir + 'manifest.json', JSON.stringify(manifest, null, 2) + '\n');
console.log(`wrote manifest.json (${manifest.length} traces)`);
