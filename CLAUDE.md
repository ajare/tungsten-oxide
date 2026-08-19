# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A native C++ (CMake/MSVC) racing track editor and driving game engine, all under `src/`. There is no browser/JS implementation in this repo; the physics core, track loading/baking, and renderer-neutral geometry are all C++ from the ground up.

- `src/core/` — the track-physics engine: schema-10/12 JSON loading, spline baking, `Simulation`/`Ship` physics, renderer-neutral geometry. See "Physics core" below.
- `src/editor/` — `track_editor`, the native ImGui/SDL2/OpenGL track editor. See "Editor conventions" below and `EDITOR_CPP_PORT_PLAN.md`.
- `src/app/` — `track_runner`, a headless CLI session host.
- `src/launcher/`, `src/applib/`, `src/tungsten-monoxide/`, `src/model-tool/` — see the root `CMakeLists.txt` header comment for the full tree and each subproject's own docs.
- `ext/willpower/` — a submodule of [`ajare/willpower`](https://github.com/ajare/willpower), providing the Willpower geometry/math libraries (`Willpower.Common`, `Willpower.Geometry`, ...). Its nested `ext/massive-poly-pusher` submodule is the single MassivePolyPusher checkout used by all native targets.
- `assets/` — bundled track textures (`assets/track/`, plus a loose `assets/test-1.png` fixture), read by `src/editor/src/TextureCache.cpp`'s `findAssetsDir()`. Stays at the repo root as a sibling of `src/`. Bundled `TextureAsset.path` values are stored relative to the repo root (e.g. `"assets/track/foo.png"`).

## Editing

When making changes to C++ code, make sure that you adhere to the .clang-format file in the root, running clang-format afterwards if necessary.  Do not run clang-format on any upstream code.

## Running / testing

From an MSVC Developer prompt, **from the repo root**:

```
cmake -S ext/willpower -B ext/willpower/build
cmake --build ext/willpower/build --config Release
cmake --build ext/willpower/build/_deps/massive-poly-pusher-build --config Release --target MppResourceParsers MppAppSupport assimp
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

See `src/README.md` for target-by-target detail. `src/core/CMakeLists.txt`'s `add_test()` commands and `src/editor/CMakeLists.txt`'s `editor_track_resources` test read golden fixtures/traces from `src/test-data/` (a sibling of `core/`/`editor/`, not nested under either) — `traces/` for physics replay, `fixtures/` for loader/mesh/geometry oracles. This corpus is a fixed, committed regression suite; there is no in-repo tool to regenerate it, so treat it as append-only unless you're prepared to hand-author or validate new fixtures directly against the C++ implementation.


## Physics core / Editor conventions

Moved to `src/core/CLAUDE.md` and `src/editor/CLAUDE.md` respectively — those load automatically whenever you're working with files under `src/core/` or `src/editor/`.
