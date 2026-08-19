# src/tungsten-monoxide — the playable game runtime

`src/tungsten-monoxide` builds `TungstenMonoxide.dll`, a Willpower-application game module loaded by `src/launcher`. It is the one module in this set that actually plays a track: it loads a Track Resource, drives `src/core`'s `GameSession` from real keyboard input, and renders the result with a chase camera and HUD via MassivePolyPusher (`mpp::`).

## Intended use

A minimal but complete playable client for **one hardcoded track**, useful as the reference integration of `core` against a real renderer and as a way to actually drive a track authored in `src/editor`. It is a read-only consumer: there is no authoring, no save, and no rebaking here — a `.mppmodel`/JSON pair out of sync with each other simply fails to load (see [Limitations](#limitations)).

## App / state structure

Entry: `src/DLL.cpp` — exports the launcher ABI (`dllGetName`, `dllOnEntry`, resource/definition factory registration). State machine: `Controller → Load → MapLoad → Play → MapUnload → Unload` (`StateControllerTungstenMonoxide.cpp`); the map name (`"NewTrack"` in namespace `"Tracks"`) is currently hardcoded there.

`StatePlayTungstenMonoxide` (`include/StatePlayTungstenMonoxide.h`) derives `applib::StatePlay` and is where everything gameplay-relevant happens:

- **`setup()`** — creates input/camera/entity-management scaffolding, loads all resources referenced by the map, then `createGameObjects()`: fetches the track model, applies debug-visibility toggles, constructs a `tox::GameSession` with the default 8-ship roster, places every ship at the map's starting-grid poses, and creates one `SceneModel3d` per ship (all sharing one loaded ship model).
- **`updateImpl()`** — `updatePreInput → updateInput → updatePreEntities → updateEntityManagement → updatePostEntities (→ updateShips) → updateCamera (→ updateChaseCamera) → updatePreRenderers → updateScreenFxManagement → updateRenderers`. The actual physics step (`GameSession::step`) happens inside the **input** phase (`updateActions`), not `updatePostEntities` — `updateShips` afterward is purely cosmetic.
- **`renderImpl()`** — flat ambient + one light, renders the scene, then the HUD.
- The applib Entity/renderer system is present but effectively bypassed — entity handler callbacks are empty stubs and entity renderers are forced invisible every frame; nothing about ship simulation goes through it.

## Track/model loading — `Map::load()`

`src/Map.cpp`. Requires a directory-based resource location (not zip-backed — `mpp::ModelSerializer` opens files by `ifstream`). Steps:

1. Sanitizes and loads the primary (Type=Track) embedded Model's `TrackData` via `tox::Track::fromTrackDataFiles()` (schema-10/12 JSON; `TRACK_MODEL_LIST_PLAN.md` Milestone 7 — a single-element call today, since only one Type=Track Model is currently supported).
2. Loads that same Model's `ModelFile` via `mpp::ModelSerializer`.
3. Builds `vector<tox::CollisionTriangle>` from the road's own collidable mesh names — derived directly from the baked Track's `GeometryKind`s (`mono::collidableGeometryBatchIds`), not read from XML — and installs it as `Track::collisionSurface` — this is the one place in the whole codebase that populates it (see `docs/core.md`'s analytical-vs-geometry split). Every Physical-typed sub-mesh of every drivable-mesh-object placement (resolved by looking the placement's `modelId` up in the parsed `<Models>` list — an embedded Model's own id, not a raw path — via `TrackCollisionBuild.h`'s `resolveModelFileReference`/`findMeshMeta`) is merged into the same BVH.
4. Derives the 8-ship starting grid analytically via `tox::StartGrid::startingGridPoses`, then **settles** each pose onto the just-built collision surface with a `nearestAlongAxis` raycast, replacing position/up with the actual contact and re-orthogonalizing forward.
5. Builds the render model mesh-by-mesh, resolving each mesh's material through the resource's own dependent-resource list; a mesh with an unresolvable material is skipped with a warning unless it's one of the road's own collidable meshes, in which case it's a fatal load error. Drivable-mesh-object placement sub-meshes render too (both Physical and Decorative — a Decorative sub-mesh just isn't in the BVH), except any mesh whose `<Visible>` is `false` in the `<Models>` list, which is skipped entirely (game-hidden — unlike `src/editor`'s own top-down canvas, which renders it regardless).

**What determines road collidability now** (retired the old flat `<TrackMeshes>` XML list — `TRACK_MODEL_LIST_PLAN.md` Milestone 7): exactly the set of baked `PathSurface`/`MeshSurface`/`ReservationWall`/`PathRail`/`MeshRail` geometry-batch ids in the compiled Track (`mono::collidableGeometryBatchIds`) — i.e. the drivable road/mesh surfaces plus their rails/walls. Zone/trigger quads and the road's shell are render-only here, never collision. Every one of those mesh names' vertex positions and normals are still bit-compared against the TrackData-baked batch (`matchesExportedFloat`) — a mismatch between the `.mppmodel` and the `.json` is a hard load failure, not a warning; this cross-check is unaffected by where the name list came from.

## Ship rendering

`ShipVisualState` (in `StatePlayTungstenMonoxide.h`) is deliberately separate from `tox::Physics` — cosmetic smoothing lives entirely on the render side:

- **Up-vector**: lerps toward `ship.renderNormal` (the live per-step surface normal `core` exposes precisely so a renderer doesn't have to use the frozen `physics.up` — see `docs/core.md`) at rate `dt·18`.
- **Position**: snaps exactly unless the frame-to-frame delta exceeds a speed-scaled threshold, in which case it lerps rather than teleporting.
- **Landing**: converts impact velocity into a **spring velocity**, not an instantaneous position offset, so contact doesn't visibly jump on the exact frame physics attaches to a surface.
- **Bob/bank/pitch**: a sine bob (suppressed while airborne or for an idle AI ship), bank eased toward `-steer · speedRatio`, pitch eased toward a function of speed — all purely cosmetic.
- `applyShipTransform()` builds an orthonormal basis from up/forward, applies the eased pitch/bank as a local rotation, and scales a shared box model to `(2.4, 0.8, 4.0)` — **there is no real ship mesh**; every ship is a stretched, identically-textured box.

**Camera**: `ReactiveCamera` (`src/ReactiveCamera.cpp`) — `updateChaseCamera()` positions the camera from the ship's *visual* (not raw physics) state, since only the visual up-vector is smoothed. Standard chase-camera math: eye behind and above the ship along its forward/up, look-at ahead of it. Camera-back distance, height, and look-ahead height are runtime-tunable via debug sliders.

## Input / controls

Keyboard only, no gamepad. `W`/`Up` throttle, `S`/`Down` brake, `A`/`Left` and `D`/`Right` steer (binary, not analogue — opposing keys cancel), `R` respawn, `Esc` exit, `J` a debug launch (jump) for ship 0, `F1` toggles the debug panel. Only ship 0 (the player) receives real input; the other 7 ships in the roster receive an idle `ControlIntent` every frame — **there is no AI**, so they sit stationary as grid dressing.

## HUD

Plain text rendering, no sprites/textures: top-left shows per-checkpoint boxes (filled once hit, or during a brief post-lap flash), lap count, current-lap time, and total time; bottom-right shows speed in km/h (`|physics.speed| * 3.6`, per `docs/core.md`'s unit convention) with a "BOOST" indicator while active. A separate debug window (`F1`) exposes render-layer visibility toggles (triggers/rails/reservation walls/wireframe — reservation walls default **visible**, since they're real gameplay geometry, not a debug aid) and live ±20%-range sliders over the player ship's handling constants for tuning exploration (not persisted back to the track).

## Resource system integration

Chain from YAML to a playable map: the launcher config names a `Directory` resource location and a `Resources.yaml`; that YAML declares a `Track`-typed resource in a `Tracks` namespace with dependent `PbrMaterialBinding` resources and a `Definition` carrying a `Models` list (`TRACK_MODEL_LIST_PLAN.md`) — one `Model` per embedded `.mppmodel`, each with its own `ModelFile` and, for the primary Type=Track one, `TrackData` plus a per-mesh `Meshes` list (`Name`/`Type`/`Visible`). `MapTungstenMonoxideDefinitionFactory` parses that definition (independently of `src/model-xml`'s TinyXML2-based parsing of the same documented shape, via `wp::DataNode` instead); `StateMapLoadTungstenMonoxide` loads the whole `Tracks` namespace, and `Map::load()` resolves each stable embedded material key through the initialized `.mpppackage`, generates tangent-capable geometry, and exposes that package-backed model as the Track's sole MPP resource. `StatePlayTungstenMonoxide::createGameObjects()` re-fetches the loaded resource and renders it with the package-owned PBR graph. The ship's model comes from a separate `Game` resource and is rebuilt as indexed PBR geometry against its `Ship.Surface` binding.

## Limitations

- **Read-only.** No authoring, no save, no rebake; a `.mppmodel`/`.json` pair that's drifted out of sync fails to load rather than being repaired.
- **Single, hardcoded track** — `"NewTrack"`/`"Tracks"` is a literal string in two places; the multi-map transition code path exists but is dead (it references a resource name that doesn't exist in the shipped config).
- **No AI, no opponents, no multiplayer.** 7 of the 8 spawned ships never move.
- **No race structure** beyond lap/checkpoint counting — no lap limit, start countdown, finish condition, or results screen. The only way to end a session is `Esc`.
- **Placeholder ship visuals** — a single scaled box model shared by every ship, one texture, no per-ship identity.
- **Audio is stubbed** — an FMOD Studio project exists in resources and is enabled in the launcher config, but the update hook that would drive it is empty; there is no engine sound, music, or impact SFX.
- **The applib Entity/renderer system is unused** — present in the build but bypassed for everything gameplay-relevant.
- **Input is keyboard-only and digital** — no gamepad, no analogue throttle/steer, no rebinding, no pause.
- **Load-path constraints**: directory-based resource locations only; track meshes must be the exact 36-byte non-indexed export layout with a matching TrackData batch; auxiliary render geometry with an unresolvable material is silently dropped rather than erroring.
- The debug physics-tuning sliders mutate the live ship's `tox::Physics` directly — a debugging aid, not a way to save tuning back to the track.
