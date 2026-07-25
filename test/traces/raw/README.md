# Raw-track parity traces

These fixtures complement the legacy baked-world traces in `test/traces/`.
Each file contains:

```json
{
  "meta": { "kind": "raw-track" },
  "sourceTrack": { "version": 10 },
  "initialState": {},
  "steps": [
    {
      "control": { "dt": 0.008333333333333333, "throttle": 1, "brake": 0, "steer": 0 },
      "outcome": { "surface": "mesh:placement-id", "railHit": false, "respawned": false },
      "after": {}
    }
  ]
}
```

JavaScript and C++ independently normalize, load, bake, and compile
`sourceTrack`; unlike the older suite, no baked world geometry is shared.
Per-step replay restores the preceding JS state and compares the complete next
state. Surface IDs, rail hits, respawns, zones, triggers, checkpoints, and other
discrete state match exactly. Continuous state uses the separately locked raw
track tolerance documented in `MESH_CPP_PORT_PLAN.md`.

`manifest.json` records scenario activity so generation fails if a fixture stops
exercising its intended branch. Regenerate both trace layers deliberately with:

```text
npm run gen-traces
```
