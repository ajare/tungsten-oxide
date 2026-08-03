# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A native C++ (CMake/MSVC) racing track editor and driving game engine, all under `cpp/`. There is no browser/JS implementation in this repo; the physics core, track loading/baking, and renderer-neutral geometry are all C++ from the ground up.

- `cpp/core/` — the track-physics engine: schema-10/12 JSON loading, spline baking, `Simulation`/`Ship` physics, renderer-neutral geometry. See "Physics core" below.
- `cpp/editor/` — `track_editor`, the native ImGui/SDL2/OpenGL track editor. See "Editor conventions" below and `EDITOR_CPP_PORT_PLAN.md`.
- `cpp/app/` — `track_runner`, a headless CLI session host.
- `cpp/launcher/`, `cpp/applib/`, `cpp/tungsten-monoxide/`, `cpp/model-tool/` — see `cpp/CMakeLists.txt`'s header comment for the full tree and each subproject's own docs.
- `cpp/willpower/` — the Willpower geometry/math libraries (`Willpower.Common`, `Willpower.Geometry`, ...). `cpp/core` no longer uses `Willpower.Geometry` (its only use, mesh topology/triangulation, was removed with `MeshRegion` — `DRIVABLE_MESH_OBJECTS_PLAN.md` Milestone 2); it's still used by `cpp/applib`/`cpp/tungsten-monoxide`.
- `assets/` — bundled track textures (`assets/track/`, plus a loose `assets/test-1.png` fixture), read by `cpp/editor/src/TextureCache.cpp`'s `findAssetsDir()`. Stays at the repo root as a sibling of `cpp/`. Bundled `TextureAsset.path` values are stored relative to the repo root (e.g. `"assets/track/foo.png"`).
- `ext/geoemetry-js/` — a git submodule (`@willpower/geometry`, https://github.com/ajare/geoemetry-js), a separate self-contained ES-module mesh/geometry library with its own `package.json`, tests, and a React/Vite editor. This is the one JavaScript codebase still in the repo; it is not part of the track app and has no bearing on `cpp/`. See `ext/geoemetry-js/README.md` for its own commands (`npm test`, `npm --prefix editor/ui run dev`, etc.) and codebase map. `cpp/willpower` references it only for vendored-copy provenance notes, not as a runtime dependency.

## Editing

When making changes to C++ code, make sure that you adhere to the .clang-format file in the root, running clang-format afterwards if necessary.  Do not run clang-format on any upstream code.

## Running / testing

From an MSVC Developer prompt, **from the repo root**:

```
cmake -S cpp -B cpp/build
cmake --build cpp/build --config Release
ctest --test-dir cpp/build -C Release --output-on-failure
```

See `cpp/README.md` for target-by-target detail. `cpp/core/CMakeLists.txt`'s `add_test()` commands and `cpp/editor/CMakeLists.txt`'s `editor_track_resources` test read golden fixtures/traces from `cpp/test-data/` (a sibling of `core/`/`editor/`, not nested under either) — `traces/` for physics replay, `fixtures/` for loader/mesh/geometry oracles. This corpus is a fixed, committed regression suite; there is no in-repo tool to regenerate it, so treat it as append-only unless you're prepared to hand-author or validate new fixtures directly against the C++ implementation.

For the `geoemetry-js` submodule, its own commands apply (`npm test`, `npm --prefix ext/geoemetry-js/editor/ui run dev`) — see `ext/geoemetry-js/README.md`.

## Physics core / Editor conventions

Moved to `cpp/core/CLAUDE.md` and `cpp/editor/CLAUDE.md` respectively — those load automatically whenever you're working with files under `cpp/core/` or `cpp/editor/`.
