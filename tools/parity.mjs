/* tools/parity.mjs — the thin top-level cross-check (CPP_PORT_PLAN.md §6): run
 * BOTH engines against the committed golden traces end-to-end.
 *
 *   node tools/parity.mjs
 *
 *  1. JS<->JS self-check (test/parity.test.js): the trace pipeline replays
 *     bit-exact, proving the oracle + lossless serialization.
 *  2. C++ per-step parity, IF the engine has been built to cpp/build/parity.exe
 *     (cmake -S cpp/core -B cpp/build -G Ninja && cmake --build cpp/build). Skipped
 *     with a note otherwise, so contributors without the C++ toolchain still get
 *     the JS half.
 *
 * The suites stay independent (npm test never needs CMake/MSVC); this is the
 * one place that runs the whole loop. */

import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const traceDir = fileURLToPath(new URL('../test/traces/', import.meta.url));
const traces = ['starter-circle.json', 'open-curve.json', 'boost-circuit.json', 'recovery-run.json']
  .map(f => traceDir + f);

function run(label, cmd, args, opts = {}) {
  process.stdout.write(`\n=== ${label} ===\n`);
  const r = spawnSync(cmd, args, { stdio: 'inherit', cwd: root, shell: false, ...opts });
  if (r.error) { console.error(`  ${label} failed to launch: ${r.error.message}`); return 1; }
  return r.status ?? 1;
}

let failed = 0;

// 1. JS<->JS parity self-check.
failed += run('JS<->JS parity (node --test)', process.execPath, ['--test', 'test/parity.test.js']) ? 1 : 0;

// 2. C++ per-step parity, if built.
const exeCandidates = [
  `${root}cpp/build/parity.exe`,
  `${root}cpp/build/parity`,
  `${root}cpp/build/Release/parity.exe`
];
const exe = exeCandidates.find(existsSync);
if (exe) {
  failed += run('C++ per-step parity', exe, traces) ? 1 : 0;
} else {
  console.log('\n=== C++ per-step parity ===');
  console.log('  SKIPPED — build the engine first:');
  console.log('    cmake -S cpp/core -B cpp/build -G Ninja && cmake --build cpp/build');
}

process.stdout.write(`\n${failed ? `FAILED (${failed} suite(s))` : 'ALL PARITY CHECKS PASSED'}\n`);
process.exit(failed ? 1 : 0);
