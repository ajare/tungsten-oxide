# Seeded random track-mesh geometry fixtures

These five schema-10 JSON tracks are generated deterministically by
`test/parity/random-track-mesh.js`. They vary rational spline controls,
banking, width/cross-section profiles, shell thickness, polygon topology and
holes, transformed mesh placements, rails, path/mesh zones, and texture
metadata.

Regenerate deliberately with:

```sh
npm run gen-random-mesh-fixtures
```

The generator writes each source JSON plus
`expected/geometry-summary.json`, produced by the JavaScript headless bake and
renderer-neutral geometry builder. `random_geometry_parity` independently
loads each JSON in C++ and compares:

- batch identity, kind, material, texture metadata, and triangle counts;
- position and UV bounds;
- total triangle area, area centroid, and oriented-area vector;
- finite/unit normals, valid indices, and opaque-white RGBA.

Equivalent mesh triangulation is allowed: geometric moments are compared
rather than triangle order. `test/random-track-mesh-parity.test.js` ensures the
committed random corpus and JS oracle remain reproducible from their seeds.
