# Context / Glossary

Canonical domain vocabulary for the track editor + driving game. Definitions
only — no implementation detail. See `CLAUDE.md` for the architecture and
`track-core.js` for the authoritative data-model spec.

## Terms

### World unit  ·  metre
One world unit **is one metre**. This is the canonical scale anchor: every
length in a track (control-point positions, widths, elevations, rail heights,
thickness) and every physics length/velocity is in metres (or metres/second).
The speedometer converts directly: `m/s × 3.6 = km/h`. Angles, curve `t`,
NURBS weight, cross-section curvature and tightness are dimensionless and carry
no unit.

### Track length
The **centerline (driven) arc length** of a path — the distance actually
travelled around it, not the control-polygon perimeter or a bounding size.
Authored tracks target **7,000–10,000 m**. "A circle of 8,000 units" means a
loop whose driven centerline measures 8,000 m (radius ≈ 1,273 m), calibrated
after baking rather than by placing control points on that radius (the NURBS
curve does not pass through its control points).

### Physics sample
One baked centerline frame that collision/physics rides on (distinct from the
render mesh, which is resampled adaptively and independently). Their count
scales with track length to hold sample spacing roughly constant, so collision
fidelity does not degrade on longer tracks.
