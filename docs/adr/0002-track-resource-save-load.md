# 0002 — Native track editor saves and opens Track resources

Status: Accepted and implemented
Date: 2026-07-29

## Context

The native C++ track editor previously exposed **Export MppModel…**. That command selected a
`.mppmodel` destination and wrote three standalone sibling files: model, schema-10 JSON, and a new
single-resource XML document. It could not merge a Track into an existing willpower
`<Resources>` document, reopen a Track through that resource contract, retain a save destination,
or protect unsaved work.

The runtime contract already treats `<TrackData>` JSON as authored source and `<ModelFile>` plus
`<TrackMeshes>` as generated runtime data. The editor should operate on that same contract rather
than presenting model generation as a disconnected export.

## Decisions

### D1 — Track resource identity is separate from editable track metadata

A Track document has two names:

- `TrackDefinition::name`, stored in schema-10 JSON and editable in Track Properties; and
- `Resource@name`, the stable identity under the logical `Tracks` namespace.

A new document captures its current metadata name (or `Track` when empty) as resource identity on
first save. Loading captures the selected Resource name. Later metadata edits never rename the
resource. Save As copies the same stable identity into another Resources document and rebinds the
editor; it does not modify the source document.

### D2 — JSON is authoritative; model geometry is generated

Open reads the safe relative `<TrackData>` file and uses it as the sole editable source. It does not
load or compare `.mppmodel` geometry. Save regenerates `.mppmodel` and `<TrackMeshes>` from the
current bake. A missing, stale, absent, or unsafe `<ModelFile>` does not block Open; Save repairs it
with a safe resource-name-derived model filename.

**Schema note (`TRACK_MODEL_LIST_PLAN.md` Milestone 5):** `<TrackData>`/`<ModelFile>` are no longer
direct children of `<Definition factory="Track">` — they live on the primary (Type=Track) entry of a
`<Models>` list instead, and the flat `<TrackMeshes>` list is retired outright (superseded by that
primary entry's own per-mesh `<Meshes>`, which the host now derives independently rather than
reading — see `docs/tungsten-monoxide.md`). The principle this decision states — JSON is the sole
editable source, geometry is always regenerated — is unchanged; only the surrounding element names
are.

### D3 — Open scans the logical Tracks namespace and exposes invalid entries

**Open Resources XML…** accepts a `<Resources>` document and scans all root-level
`<Namespace name="Tracks">` blocks as one logical namespace. Every direct `Resource type="Track"`
appears in a chooser. Invalid entries remain visible but disabled with their validation error.

A loadable entry has one `Definition factory="Track"`, a safe relative `<TrackData>` path, and
parseable schema-10 JSON. Duplicate resource identities are ambiguous and disabled. Multiple
Tracks namespace blocks are accepted; insertion uses the first and creates one only when none
exists.

### D4 — Save replaces one complete Track Resource and preserves the rest

Save generates and replaces the complete matching `Resource type="Track"` element: dependencies,
`ModelFile`, `TrackData`, and `TrackMeshes` are one editor-owned unit. All unrelated XML elements,
attributes, declarations, and comments remain. TinyXML2 may normalize whitespace/indentation; byte
formatting is not part of the contract.

**Extended by `TRACK_MODEL_LIST_PLAN.md` Milestone 5.3:** "one editor-owned unit" now applies at
per-`<Model>` granularity within the `<Models>` list, not the whole `Definition` — Save regenerates
only the primary Model fresh every time; every other `<Model>` entry (Physical/Decorative props,
Milestone 6's "Load Model") is sourced from the in-session `TrackDefinition::models` and written back
via `cpp/model-xml`'s `writeModelFragment`, completely unedited by the save path itself.

An existing malformed XML document or one whose root is not `<Resources>` is never overwritten.
A nonexistent or empty destination is initialized as a Resources document. A same-name non-Track
resource or duplicate identity blocks Save.

### D5 — Sidecars are safe and resource-relative

Normal Save preserves the loaded safe `<TrackData>` and `<ModelFile>` references. First Save and
Save As create sanitized `<resource-name>.json` and `<resource-name>.mppmodel` beside the selected
XML. Resource names retain their exact UTF-8 text; only filesystem stems are sanitized.

Absolute paths, rooted paths, and references escaping through `..` are rejected. Newly generated
sidecars never silently replace unrelated files; collisions join the explicit overwrite
confirmation.

### D6 — XML, JSON, and model commit as one transaction

Save is prepared fully in memory, including a verification parse of the updated XML. All three
files are written to temporary paths, existing destinations are moved to backups, and all three
new files are installed. Any failure restores every previous destination. The editor updates its
binding and clean revision only after the entire transaction succeeds.

Before bound Save, the XML is reread so unrelated external edits are retained. A changed/deleted/
duplicated bound Resource is an XML conflict. An externally changed JSON sidecar is a source
conflict. The generated `.mppmodel` is overwritten without a content conflict check.

### D7 — Save/Save As replace model export in the document workflow

The File menu provides:

- New
- Open Resources XML… (`Ctrl+O`)
- Save (`Ctrl+S`)
- Save As… (`Ctrl+Shift+S`)
- Refresh materials from XML
- the existing JSON, mesh, and USD interchange commands

There is no Export MppModel command. Import Track JSON creates an unbound dirty document; Export
Track JSON is an interchange copy and neither changes the binding nor marks the document clean.

New, Open, Import, and Exit protect dirty work with Save / Discard / Cancel. New documents are
unbound but clean until edited. Save on an unbound document invokes Save As.

### D8 — Material assignments are preserved and refreshed explicitly

The authoritative material catalog remains the Resources XML configured by `editor.ini`, not the
Track XML selected by Open. Unknown non-empty material assignments are preserved and shown as
unavailable instead of silently changed. Save is blocked until they resolve or are reassigned.

**Refresh materials from XML** exists in both the File menu and Materials panel. It reloads only the
configured material catalog. Success can make a preserved assignment valid without rewriting the
track. Failure leaves the prior catalog active.

### D9 — Collision and conflict interaction is domain-specific

The Resources Save dialog suppresses Windows' generic whole-file overwrite warning because Save
merges one Resource. The editor instead confirms the exact Track resource and unrelated sidecars
that would be replaced. Bound Save does not repeatedly confirm its own outputs.

External source conflicts offer Reload, Save As, or Cancel. Reload discards the in-memory document
and adopts the current bound JSON; Save As keeps current edits and writes a copy elsewhere.

## Consequences

- A runtime Track resource is now the native editor's document boundary.
- Resource identity remains safe for external references even when display metadata changes.
- Existing multi-resource XML files can be edited without discarding unrelated declarations.
- Saving is more expensive than a single-file write but cannot intentionally leave a mixed
  generation of XML/JSON/model.
- Resource renaming is deliberately absent; it requires a future explicit operation with reference
  and collision handling.
