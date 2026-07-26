/* web/tools/parity.mjs — the thin top-level cross-check (CPP_PORT_PLAN.md §6): run
 * BOTH engines against the committed golden traces end-to-end.
 *
 *   npm run parity          (from web/)
 *   node tools/parity.mjs   (from web/, equivalent)
 *
 *  1. JS<->JS self-check (test/parity.test.js): the trace pipeline replays
 *     bit-exact, proving the oracle + lossless serialization.
 *  2. C++ baked-world and independently loaded raw-track parity, plus seeded
 *     random JSON-to-render-geometry parity, IF the engine has been built from cpp/ (combined)
 *     or cpp/core (standalone). Skipped
 *     with a note otherwise, so contributors without the C++ toolchain still get
 *     the JS half.
 *
 * The suites stay independent (npm test never needs CMake/MSVC); this is the
 * one place that runs the whole loop. */

import { spawnSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

// Two roots: `root` is web/ (where node --test/test/traces/package.json live -- this file is
// web/tools/parity.mjs, so one level up); `repoRoot` is the actual repo root, where cpp/build
// lives (cpp/ stays there, a sibling of web/, not moved alongside the JS/HTML implementation --
// see CLAUDE.md's "What this is").
const root = fileURLToPath(new URL('..', import.meta.url));
const repoRoot = fileURLToPath(new URL('../..', import.meta.url));
const traceDir = fileURLToPath(new URL('../test/traces/', import.meta.url));
const traces = ['starter-circle.json', 'open-curve.json', 'boost-circuit.json', 'recovery-run.json']
  .map(f => traceDir + f);
const rawManifest = JSON.parse(readFileSync(traceDir + 'raw/manifest.json', 'utf8'));
const rawTraces = rawManifest.map(entry => traceDir + 'raw/' + entry.file);
const rawSessionInitManifest = JSON.parse(readFileSync(traceDir + 'raw-session/init/manifest.json', 'utf8'));
const rawSessionInitTraces = rawSessionInitManifest.map(entry => traceDir + 'raw-session/init/' + entry.file);
const rawSessionStepManifest = JSON.parse(readFileSync(traceDir + 'raw-session/steps/manifest.json', 'utf8'));
const rawSessionStepTraces = rawSessionStepManifest.map(entry => traceDir + 'raw-session/steps/' + entry.file);

function run(label, cmd, args, opts = {}) {
  process.stdout.write(`\n=== ${label} ===\n`);
  const r = spawnSync(cmd, args, { stdio: 'inherit', cwd: root, shell: false, ...opts });
  if (r.error) { console.error(`  ${label} failed to launch: ${r.error.message}`); return 1; }
  return r.status ?? 1;
}

let failed = 0;

// 1. JS<->JS parity self-check.
failed += run('JS<->JS parity (node --test)', process.execPath, ['--test', 'test/parity.test.js']) ? 1 : 0;

// 2. C++ baked-world and raw-track per-step parity, if built.
const exeCandidates = [
  `${repoRoot}cpp/build/core/Release/parity.exe`,  // combined MSVC build
  `${repoRoot}cpp/build/core/parity`,              // combined single-config build
  `${repoRoot}cpp/build/parity.exe`,               // standalone core builds
  `${repoRoot}cpp/build/parity`,
  `${repoRoot}cpp/build/Release/parity.exe`
];
const exe = exeCandidates.find(existsSync);
if (exe) {
  failed += run('C++ baked-world + raw-track parity', exe, [...traces, ...rawTraces]) ? 1 : 0;
  const geometryCandidates = [
    `${repoRoot}cpp/build/core/Release/random_geometry_parity.exe`,
    `${repoRoot}cpp/build/core/random_geometry_parity`,
    `${repoRoot}cpp/build/random_geometry_parity.exe`,
    `${repoRoot}cpp/build/random_geometry_parity`,
    `${repoRoot}cpp/build/Release/random_geometry_parity.exe`
  ];
  const geometryExe = geometryCandidates.find(existsSync);
  if (geometryExe) {
    failed += run('C++ random track-mesh geometry parity', geometryExe,
      [`${root}test/fixtures/random-track-mesh`]) ? 1 : 0;
  } else {
    console.log('\n=== C++ random track-mesh geometry parity ===');
    console.log('  SKIPPED — rebuild the engine to add random_geometry_parity');
  }
  const rawSessionCandidates = [
    `${repoRoot}cpp/build/core/Release/raw_session_parity.exe`,
    `${repoRoot}cpp/build/core/raw_session_parity`,
    `${repoRoot}cpp/build/raw_session_parity.exe`,
    `${repoRoot}cpp/build/raw_session_parity`,
    `${repoRoot}cpp/build/Release/raw_session_parity.exe`
  ];
  const rawSessionExe = rawSessionCandidates.find(existsSync);
  if (rawSessionExe) {
    failed += run('C++ raw-session parity (native ship/session init + stepping)', rawSessionExe,
      [...rawSessionInitTraces, ...rawSessionStepTraces]) ? 1 : 0;
  } else {
    console.log('\n=== C++ raw-session parity ===');
    console.log('  SKIPPED — rebuild the engine to add raw_session_parity');
  }
} else {
  console.log('\n=== C++ per-step parity ===');
  console.log('  SKIPPED — build the engine first (from the repo root, cpp/ is a sibling of web/):');
  console.log('    cmake -S cpp -B cpp/build && cmake --build cpp/build --config Release');
}

process.stdout.write(`\n${failed ? `FAILED (${failed} suite(s))` : 'ALL PARITY CHECKS PASSED'}\n`);
process.exit(failed ? 1 : 0);
