/* Regenerate the committed baked-world and raw-track traces in test/traces/.
 * Deliberate, reviewable regeneration only — run when physics, native loading,
 * or the parity corpus is intentionally changed:
 *
 *   npm run gen-traces
 *
 * The traces are committed fixtures read by BOTH engines (the JS self-check and
 * the C++ parity replayer). Baked traces isolate runtime math; raw traces force
 * independent current-schema loading and baking before replay. */

import { writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { installTrackCore } from '../test/parity/loadcore.js';

installTrackCore();
const { buildTrace } = await import('../test/parity/trace.js');
const { tracks } = await import('../test/parity/tracks.js');
const { rawScenarios, buildRawTrace, validateRawActivity } = await import('../test/parity/raw-traces.js');

const outDir = fileURLToPath(new URL('../test/traces/', import.meta.url));
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

const rawDir = outDir + 'raw/';
mkdirSync(rawDir, { recursive: true });
const rawManifest = [];
for (const scenario of rawScenarios()) {
  const trace = buildRawTrace(scenario.track, scenario);
  const activity = validateRawActivity(trace, scenario.require);
  const file = `${scenario.name}.json`;
  writeFileSync(rawDir + file, JSON.stringify(trace) + '\n');
  rawManifest.push({ file, steps: trace.steps.length, ...activity });
  console.log(`wrote raw/${file}: ${trace.steps.length} steps, ${activity.railHits} rail hits`);
}
writeFileSync(rawDir + 'manifest.json', JSON.stringify(rawManifest, null, 2) + '\n');
console.log(`wrote raw/manifest.json (${rawManifest.length} traces)`);
