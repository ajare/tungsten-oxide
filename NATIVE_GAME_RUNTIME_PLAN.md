# Native Game Runtime — Outstanding Non-Graphics Work

Status: **not implemented**. The schema-10 loader, track bake, mesh compilation, renderer-neutral
geometry, and per-ship simulation are complete. This document records the remaining JavaScript game
responsibilities needed for a self-contained native game runtime, excluding graphics and presentation.

Related completed plans: `CPP_PORT_PLAN.md` and `MESH_CPP_PORT_PLAN.md`.

## 1. Current boundary

C++ can:

- load and normalize a schema-10 track;
- bake spline paths, meshes, zones, and triggers;
- expose renderer-neutral track geometry;
- advance one initialized `Ship` by one supplied physics timestep;
- handle corridor/mesh driving, rails, ledges, airborne motion, landing, boosts, checkpoints, laps,
  and respawn recovery.

C++ does **not** yet create a playable race session. The caller must currently construct and
initialize each `Ship`, populate race metadata and its start pose, apply authored handling, choose
substeps, provide controls, and infer gameplay events from changed state.

The raw-track parity corpus proves native loading, baking, and per-step simulation, but deliberately
loads a JS-created `initialState`. It therefore does not prove native ship/session initialization or
the browser game's frame orchestration.

## 2. Required work

### 2.1 Native ship initialization

Add one canonical initialization path instead of requiring callers to assemble `Ship` fields.
It must:

- copy `TrackDefinition::handling` into `Physics`:
  - `maxSpeed`;
  - `accel`;
  - `turnSpeed` converted from degrees/second to `turnRate` radians/second;
  - `weight`;
- initialize `Race::intermediateIds` and `Race::finishId` from compiled checkpoint triggers;
- initialize zone and trigger detection state;
- initialize `lastCheckpoint`, boost state, and visual-state defaults consistently;
- assign and place the ship at a valid start pose.

This should be a public factory/helper rather than behavior hidden in a test or executable.

### 2.2 Authored start pose and starting grid

Port the non-rendering behavior currently split between `js/track-bake.js`, `js/ship-grid.js`, and
`js/track-game.js`:

- resolve `track.start.path`, `point`, and `reverse` against the baked path;
- locate the corresponding centerline frame;
- interpolate a frame by distance behind the authored start;
- generate the alternating two-column staggered grid;
- compress lateral spacing on narrow roads using wall margins and ship half-width;
- settle each analytical slot onto the same sampled curved surface used by physics;
- produce deterministic position/up/forward poses for any roster size.

The pure grid constants and ordering should remain renderer- and platform-independent. Preserve the
current default of eight ships unless a later game-design decision changes it.

### 2.3 Session/roster ownership

Add a renderer-neutral session layer above `Simulation`. A likely shape is a `GameSession` that owns:

- an immutable compiled `Track`/`Simulation`;
- the player and other ships;
- stable ship IDs and controller/player designation;
- race clocks and session time;
- pending gameplay events.

It should create and reset a complete roster, expose ships without graphics objects, and step every
ship independently. Ship-to-ship collision is not present in the JavaScript game and is not part of
this port.

### 2.4 Controls and frame orchestration

Keep platform input outside the physics core, but define a neutral intent record:

```cpp
struct ControlIntent {
  double throttle{0};
  double brake{0};
  double steer{0};
  bool respawn{false};
};
```

The session/runtime layer should reproduce the JavaScript host behavior:

- sample one intent per ship per rendered frame;
- clamp an abnormally long frame delta according to game policy;
- divide moving ships into equal substeps no larger than `MAX_PHYSICS_STEP`;
- preserve the stationary-idle optimization only if measurement shows it is useful;
- process explicit and automatic respawns;
- return the final state and accumulated events after all substeps.

Keyboard, gamepad, networking, and other platform mappings should translate into `ControlIntent`
outside `core`.

### 2.5 Race timing

C++ already implements checkpoint ordering and lap increments, but `Race` intentionally omits the
JavaScript wall-clock fields. Add deterministic session-time equivalents for:

- total race start time;
- current lap start time;
- latest lap duration, if useful to the host;
- checkpoint/lap flash expiry or an event from which presentation can derive it.

Use caller-supplied or accumulated monotonic time; do not read a platform clock inside deterministic
physics. Define reset/restart behavior explicitly.

### 2.6 Gameplay events

JavaScript injects `onTriggerFired`, while C++ currently mutates state without exposing a comparable
notification. Add an event result or queue with, at minimum:

- trigger fired, including dummy triggers, ship ID, trigger ID, and crossing direction;
- checkpoint accepted;
- lap completed;
- explicit/automatic respawn;
- rail impact if consumers need impact feedback.

Boost entry/exit and landing events may also be useful, but should only be added with precise edge
semantics. Events must be emitted once even when one frame contains multiple physics substeps.

Do not make the deterministic core call logging, audio, UI, or renderer callbacks directly.

### 2.7 Native application track lifecycle

The browser game still owns:

- choosing its built-in default track;
- file-picker import;
- `localStorage` loading and editor live-update messages;
- user-facing load-error reporting.

A native executable will need platform-specific equivalents, but these should remain outside
`cpp/core`. The core's existing `Track::fromFile()` and structured errors/warnings are the boundary.

## 3. Presentation-only JavaScript that is not part of this work

The following remains unported but is intentionally outside this non-graphics runtime milestone:

- three.js scene, materials, image decoding, texture upload, and ship meshes;
- camera behavior;
- HUD, checkpoint lights, race-time formatting, and minimap drawing;
- trigger debug geometry, wireframe, and rail-visibility controls;
- render-position/up smoothing, hover bob, cosmetic bank/pitch, and visual landing-bounce spring;
- browser DOM events and file/storage UI.

Some cosmetic values currently live in `Physics` because of the historical JavaScript extraction.
A future implementation may move them to a separate presentation state rather than updating them in
`Simulation`.

## 4. Explicitly not required

Unless separately planned, this work does not include:

- historical track-schema migration;
- native track saving or editor operations;
- preservation of unknown/editor-only JSON fields;
- procedural track generation;
- racing AI (the current JavaScript "AI" controller is an idle placeholder);
- ship-to-ship collision;
- graphics, audio, networking, or platform input APIs.

## 5. Suggested implementation order

1. Port grid/start-pose helpers and add direct JS/C++ fixture comparisons.
2. Add a canonical ship factory that applies handling and initializes race/detection state.
3. Add `ControlIntent` and a deterministic multi-ship `GameSession` with substepping.
4. Add session-time race clocks and structured gameplay events.
5. Add end-to-end session traces that begin with raw schema-10 JSON and no serialized JS ship state.
6. Integrate the session into a native executable or embedding host.

## 6. Verification and completion criteria

The work is complete when:

- a caller can load a schema-10 file and create a correctly initialized race session with one API
  path;
- authored handling and start direction affect native ships without manual field assignment;
- native and JavaScript starting-grid poses match within a measured tolerance for closed, open,
  banked, narrow, and reversed starts;
- frame/substep orchestration matches JavaScript for variable frame deltas and respawn requests;
- dummy triggers, checkpoints, laps, and respawns produce exact ordered events;
- race timing is deterministic under a supplied timestep sequence;
- a new raw-session parity layer starts from track JSON plus controls only—no JS-baked world and no
  JS-created `initialState`;
- all existing baked-world and raw-track parity gates remain unchanged and passing;
- the runtime layer has no renderer, DOM, image, audio, or platform-input dependency.
