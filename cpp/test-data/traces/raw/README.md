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

These traces independently normalize, load, bake, and compile
`sourceTrack` in C++ against a fixed recorded reference; unlike the older suite, no baked world
geometry is shared. Per-step replay restores the preceding recorded state and compares the complete
next state. Surface IDs, rail hits, respawns, zones, triggers, checkpoints, and other
discrete state match exactly. Continuous state uses the separately locked raw
track tolerance documented in `CLAUDE.md`.

`manifest.json` records scenario activity so generation fails if a fixture stops
exercising its intended branch. This corpus is a fixed, committed regression suite; there is no
in-repo tool to regenerate it.
