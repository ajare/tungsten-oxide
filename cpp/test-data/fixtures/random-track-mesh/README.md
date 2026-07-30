# Seeded random track-mesh geometry fixtures

These five schema-10 JSON tracks vary rational spline controls,
banking, width/cross-section profiles, shell thickness, polygon topology and
holes, transformed mesh placements, rails, path/mesh zones, and texture
metadata.

This is a fixed, committed regression corpus; there is no in-repo tool to regenerate it.

Each source JSON has a matching `expected/geometry-summary.json`, a recorded reference bake.
`random_geometry_parity` independently loads each JSON in C++ and compares:

- batch identity, kind, material, texture metadata, and triangle counts;
- position and UV bounds;
- total triangle area, area centroid, and oriented-area vector;
- finite/unit normals, valid indices, and opaque-white RGBA.

Equivalent mesh triangulation is allowed: geometric moments are compared
rather than triangle order.
