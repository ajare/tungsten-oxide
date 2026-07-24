# Ubiquitous Language

## Track structure

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Track** | The complete authored racing world, including paths, mesh regions, starts, zones, triggers, and texture references. | Course, level, map |
| **Path** | An ordered open or closed spline ribbon that forms a drivable road surface. | Curve, road, spline |
| **Control point** | An authored point that contributes one typed property to a path, such as position, roll, width, or cross-section. | Node, handle, point |
| **Mesh region** | A flat, rigidly placed drivable area used for shapes that a swept path cannot represent. | Mesh, pad, arena, plaza |
| **Start** | The single authored path location and direction from which the runtime starting grid is derived. | Spawn, start point |
| **Starting grid** | The runtime set of staggered surface-conforming poses assigned to the ship roster around the authored start. | Spawn grid, grid layout |
| **Track surface** | Any drivable surface supplied by a path or mesh region. | Ground, road |
| **Ledge** | An unrailed boundary from which a ship can become airborne. | Open edge, drop-off |
| **Mesh section** | A generated route section in which one or more mesh platforms connect an outgoing open path end to a lower receiving path end. | Gap, mesh gap, split section |
| **Platform sequence** | A mesh section containing two to four separated drivable platforms. | Platform chain, mesh sequence |
| **Launch ramp** | A short inclined open path that gives a ship upward velocity before a level or rising platform transition. | Ramp mesh, jump ramp |

## Ships and movement

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Ship** | An independently simulated racing vehicle with its own motion and interaction state. | Car, craft, vehicle |
| **Player ship** | The one ship controlled by user input and followed by the camera and HUD. | Player, user ship |
| **AI ship** | A non-player ship whose input is supplied by an AI controller. | Bot, NPC, opponent car |
| **Ship roster** | The runtime-only collection of the player ship and AI ships participating in a race. | Players, fleet, ship list |
| **Controller** | The source of steering and throttle intent for one ship. | Driver, input handler |
| **Idle controller** | An AI controller that supplies no steering or throttle intent. | Stationary AI, dummy driver |
| **Grounded** | The motion state in which a ship is constrained to its current track surface. | On track, attached |
| **Airborne** | The ballistic motion state entered after a ship leaves a ledge or an open path end. | Falling, flying |
| **Impact** | A ship's collision with a solid rail boundary. | Hit, crash |
| **Bounce** | The reflected motion produced by restitution after an impact. | Rebound, knockback |
| **Path guard rail** | A solid lateral boundary along a path that reflects a ship's into-wall velocity. | Wall, spline rail, guardrail |
| **Region rail** | A solid finite-height boundary edge authored on a mesh-region asset. | Mesh rail, wall, guard rail |

## Race progression

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Trigger** | A vertical gate that detects a swept ship crossing and owns independent armed state per ship. | Gate, sensor |
| **Checkpoint** | A race-progression trigger that must be crossed in its authored sequence. | Gate, marker |
| **Finish checkpoint** | The unique checkpoint that starts timing and completes a lap after all intermediate checkpoints are satisfied. | Finish line, finish trigger |
| **Intermediate checkpoint** | A non-finish checkpoint required between successive crossings of the finish checkpoint. | Split, checkpoint gate |
| **Lap** | One ordered traversal from the finish checkpoint through every intermediate checkpoint and back through the finish checkpoint. | Circuit, round |
| **Lap progress** | One ship's current position in the required checkpoint sequence. | Checkpoint state, race progress |
| **Lap timer** | One ship's wall-clock measurement for its current lap. | Stopwatch, race timer |
| **Checkpoint respawn** | Repositioning a ship at its most recently accepted checkpoint. | Reset, restart, spawn |
| **Manual respawn** | The player-requested checkpoint respawn initiated by the respawn control. | Reset, manual reset |

## Interactive areas

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Zone** | A flat surface area that applies an effect when a grounded ship enters it on the zone's host surface. | Pad, area trigger |
| **Boost zone** | A zone that temporarily raises a ship's speed and speed cap. | Speed pad, accelerator |
| **Start-grid zone** | A checkered visual zone with no race mechanic. | Start grid, finish line |
| **Host surface** | The specific path or mesh region to which a zone or trigger is anchored. | Parent, owner, attachment |

## Texture authoring

| Term | Definition | Aliases to avoid |
| --- | --- | --- |
| **Texture asset** | Track metadata describing a referenced image and its tile dimensions without containing image bytes. | Embedded texture, image blob |
| **Texture path** | The filename or relative filepath stored in track JSON for loading a texture image. | Data URL, file data, absolute path |
| **Texture tile** | One selectable rectangular cell within a texture asset's image grid. | Sprite, texture image |
| **In-memory preview** | A session-only display of a selected local image that is never serialized into the track. | Embedded image, saved preview |

## Relationships

- A **Track** contains zero or more **Paths** and **Mesh regions**, but exactly one authored **Start**.
- A **Starting grid** is derived from the **Start** and assigns one pose to each **Ship** in the **Ship roster**.
- A **Ship roster** contains exactly one **Player ship** and zero or more **AI ships**.
- Every **Ship** has exactly one **Controller**, independent race state, trigger state, zone state, and motion state.
- A **Path** contains ordered typed **Control points** and may have lateral **Path guard rails**.
- A **Mesh region** may have zero or more **Region rails**; an unrailed boundary is a **Ledge**.
- A **Mesh section** contains either one **Mesh region** or one **Platform sequence** and may contain **Launch ramps**.
- A valid checkpoint set contains exactly one **Finish checkpoint** and zero or more ordered **Intermediate checkpoints**.
- Each **Ship** owns its own **Lap progress**, **Lap timer**, and most recent **Checkpoint respawn** location.
- A **Zone** or **Trigger** belongs to exactly one **Host surface**.
- A **Texture asset** contains one **Texture path** and one or more **Texture tiles**, but no image bytes.

## Example dialogue

> **Dev:** "When an **AI ship** crosses an **Intermediate checkpoint**, does that advance the player's **Lap progress**?"
>
> **Domain expert:** "No. Every **Ship** owns its own checkpoint sequence and **Lap timer**; only the **Player ship** is shown in the HUD."
>
> **Dev:** "If that ship then hits a **Path guard rail**, should it stop at the boundary?"
>
> **Domain expert:** "It must remain inside the **Track surface**, but its into-wall velocity produces a **Bounce**. It must not stick at the rail."
>
> **Dev:** "And a texture chosen in the editor is saved as a **Texture asset**?"
>
> **Domain expert:** "Yes, but only its **Texture path** and tile metadata are saved; the selected file is merely an **In-memory preview**."

## Flagged ambiguities

- "Rail" and "guard rail" have been used interchangeably. Use **Path guard rail** for a path's lateral collision boundary and **Region rail** for a flagged mesh-region edge; use **rail** alone only when the distinction is irrelevant.
- "Trigger" has referred both to generic debug gates and race checkpoints. Use **Trigger** for the general vertical-gate concept and **Checkpoint** only for a trigger participating in lap progression.
- "Start grid" can mean the runtime ship arrangement or the checkered visual zone. Use **Starting grid** for ship poses and **Start-grid zone** for the visual marker.
- "Respawn" and "reset" have been used as synonyms. Use **Checkpoint respawn** for restoring a ship to race progress and **Manual respawn** for the player's request; reserve "reset" for clearing runtime state.
- "Path", "curve", "road", and "spline" have referred to the same authored ribbon. Use **Path** as the domain entity and "spline" only when discussing its interpolation geometry.
- "Mesh" can mean either raw geometry, a placed region, or rendered triangles. Use **Mesh region** for the drivable domain entity and qualify asset geometry or rendered geometry explicitly.
- A browser file dialog does not reveal the selected file's absolute filepath. **Texture path** therefore means the stored filename or available relative path, not an OS path; authors must ensure that reference is loadable beside the served track application.
