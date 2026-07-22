
import * as TrackMesh from './track-mesh.js';
import { exportTrackToUSDA, sanitizeFileStem } from './usd-export.js';

// ---------- Editor state ----------
// track.paths: [{ closed, points }, ...] — a track is one or more paths, each
// either a closed loop or an open curve. `points` is a single array of TYPED
// control points (type: 'position' | 'roll' | 'width', see track-core.js);
// TrackCore.splitPoints(path.points) derives the three plain arrays the math
// functions consume. curParts()/parts(path) are the editor's own shorthand
// for that call.
let track = TrackCore.cloneTrack(TrackCore.STARTER_TRACK);

// ---------- Undo / redo ----------
// Whole-track snapshots (deep clones via TrackCore.cloneTrack, so stack
// entries never alias the live `track` or each other). Every discrete
// mutation calls pushUndo() once, right before it changes anything, capturing
// the PRE-edit state; a continuous gesture (dragging a point, typing in a
// number field) calls it once at the start of the gesture, not per tick, so
// undo steps line up with what a user thinks of as "one edit".
let undoStack = [];
let redoStack = [];
const MAX_HISTORY = 30;
// Selecting a point (mousedown on it) doesn't itself mutate `track` -- only
// actually dragging it does. Set false whenever a drag-capable selection is
// made; the first mousemove tick of that drag pushes undo and flips it true,
// so merely clicking to select never records a no-op undo step, and a real
// drag still collapses into exactly one step regardless of how many mousemove
// ticks it spans.
let dragMutated = false;
function pushUndo() {
  undoStack.push(TrackCore.cloneTrack(track));
  if (undoStack.length > MAX_HISTORY) undoStack.shift();
  redoStack.length = 0; // a fresh edit invalidates whatever was available to redo
}
function applyHistoryState(t) {
  track = t;
  selectedPointId = null; syncSelectionToId();
  segSel = null; joinSel = []; rollSel = null; widthSel = null; crossSectionSel = null;
  joinDragFrom = null; joinDragTarget = null; joinDragScreen = null; joinDragStartScreen = null;
  dragging = null;
  // Restored assets may differ from the ones cached, so drop the live meshes
  // and let them rebuild from the snapshot's JSON.
  clearMeshSelection();
  invalidateMeshCache();
  refresh();
}
function undo() {
  if (!undoStack.length) return;
  redoStack.push(TrackCore.cloneTrack(track));
  if (redoStack.length > MAX_HISTORY) redoStack.shift();
  applyHistoryState(undoStack.pop());
}
function redo() {
  if (!redoStack.length) return;
  undoStack.push(TrackCore.cloneTrack(track));
  if (undoStack.length > MAX_HISTORY) undoStack.shift();
  applyHistoryState(redoStack.pop());
}
function updateUndoRedoButtons() {
  const u = document.getElementById('undoBtn'), r = document.getElementById('redoBtn');
  if (u) u.disabled = undoStack.length === 0;
  if (r) r.disabled = redoStack.length === 0;
}

let selectedPointId = null;       // selected POSITION control point identity
let sel = { path: 0, point: 0 };  // compatibility cache for selected point occurrence
let segSel = null;                // selected segment for deletion: { path, i }
let joinSel = [];                 // up to 2 selected vertices for Connect: { path, point, end? }
let joinDragFrom = null;          // { path, point, end } source endpoint of an in-progress shift-drag connect
let joinDragTarget = null;        // { path, point } currently-hovered valid drop point, or null
let joinDragScreen = null;        // {x,y} current mouse position (screen space) while shift-dragging to connect
let joinDragStartScreen = null;   // {x,y} where the shift-drag began, to tell an intentional drag from a click
const JOIN_DRAG_MIN_PX = 12;      // minimum screen-space drag distance before an empty-space drop extends the curve
let editMode = 'edit';
let createDraft = [];             // position point objects for a new curve draft
// Roll/width points are selected by direct object reference (into
// path.points), not index, so selection survives re-sorting while dragging.
let rollSel = null;
let widthSel = null;
let crossSectionSel = null;
let dragging = null;              // 'top' | 'elev' | 'rollElev' | 'rollTop' | 'widthTop' | 'crossSectionTop' | 'joinDrag' | null
const WIDTH_COLOR = '#b6ff3c';    // distinct from roll's lean tint and position's elevation tint
const CROSS_SECTION_COLOR = '#d58cff';
// Top-down rendering: 'banked' draws the actual banked edge geometry (tinted
// by lean, as before); 'flat' ignores roll for edge shape (width only, as if
// roll were always 0) and instead colours the ribbon itself by roll value.
let renderMode = 'banked';
let pointFilters = { position: true, roll: true, width: true, crossSection: true };
// Debug overlay: the uniform-in-parameter N_DEFAULT (=400) baked frames the
// editor previews with (buildCenterline), rendered per path and made selectable
// so their exact baked values can be inspected. The GAME rides on the same
// curve but samples it adaptively by track length (TrackCore.adaptiveSampleCount),
// so its live frame COUNT differs -- the shape/values shown here are identical,
// only the density differs. Purely a viewer -- these frames are derived, not
// authored, so the panel is read-only.
let showPhysicsPoints = false;
let physicsSel = null;            // { path, index } into a path's baked frames
let topZoom = 1;                  // multiplier over the auto-fit top-down view
let gridSize = 32;
let snapToGrid = false;
let topPan = { x: 0, y: 0 };      // screen-pixel offset from the auto-fit center
let panLast = null;               // last mouse position while right-drag panning
let topPanned = false;            // suppresses the context menu after a pan drag
const ROLL_HANDLE_MARGIN = 6;     // keeps roll handles from rendering off the top/bottom edge
// Elevation panel collapsed to a title bar at the bottom of the window. Declared
// here, with the rest of the view state, rather than beside its wiring at the
// end of the file: drawElev() reads it, and the first draw happens before that
// point, which for a `let` would be a temporal-dead-zone throw rather than a
// harmless undefined.
const ELEV_COLLAPSED_KEY = 'web3d.editorElevCollapsed';
let elevCollapsed = false;
try { elevCollapsed = localStorage.getItem(ELEV_COLLAPSED_KEY) === '1'; } catch { /* private mode */ }

function parts(path) { return TrackCore.splitPoints(path.points); }

// ---------- Mesh regions ----------
// track.meshAssets holds pristine geometry-js mesh JSON; track.meshes holds
// rigid placements of it. The JSON in `track` stays authoritative (it is what
// undo snapshots and export read), and `meshCache` is just a live geometry-js
// Mesh per asset so we are not reparsing on every frame. Any edit that changes
// geometry -- only rail flags do -- mutates the live Mesh and then writes it
// straight back to the asset JSON, so the two never drift.
const MESH_EDGE_PICK_PX = 8;        // click tolerance when picking rail edges
const MESH_ELEV_PICK_PX = 6;        // grab tolerance for the elevation line
let meshCache = new Map();          // assetId -> geometry-js Mesh
let selectedMeshId = null;          // placement id
let railSel = null;                 // { meshId, edgeId } in Rails mode
let meshDragOffset = null;
let meshRotateStart = null;         // { originRotation, startAngle } for shift-drag rotation

function invalidateMeshCache() { meshCache = new Map(); }

function assetMesh(assetId) {
  if (meshCache.has(assetId)) return meshCache.get(assetId);
  const asset = (track.meshAssets || {})[assetId];
  if (!asset) return null;
  let mesh = null;
  try {
    mesh = TrackMesh.meshFromJSON(asset.mesh);
  } catch (err) {
    console.warn(`editor: mesh asset "${assetId}" failed to load`, err);
  }
  meshCache.set(assetId, mesh);
  return mesh;
}

// Persist rail-flag edits back into the asset JSON so they land in undo
// snapshots and exports.
function writeBackAsset(assetId) {
  const mesh = meshCache.get(assetId);
  const asset = (track.meshAssets || {})[assetId];
  if (mesh && asset) asset.mesh = TrackMesh.meshToJSON(mesh);
}

function meshPlacements() { return track.meshes || []; }
function findPlacement(id) { return meshPlacements().find(m => m.id === id) || null; }
function selectedPlacement() { return selectedMeshId ? findPlacement(selectedMeshId) : null; }

// Compiled (world-space) form of every placement, rebuilt per draw. Cheap:
// geometry-js caches each polygon's triangulation, so this is mostly the rigid
// transform, and a rigid transform never invalidates a triangulation.
function compiledMeshes() {
  const out = [];
  for (const placement of meshPlacements()) {
    const mesh = assetMesh(placement.asset);
    if (!mesh) continue;
    out.push({ placement, mesh, compiled: TrackMesh.compile(mesh, placement) });
  }
  return out;
}

function clearMeshSelection() { selectedMeshId = null; railSel = null; meshDragOffset = null; meshRotateStart = null; }

// ---------- Texture assets ----------
let texturePanelOpen = false;
let textureTileEditArmed = null;
function escapeHtml(s) { return String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
function textureAssets() { if (!track.textureAssets || typeof track.textureAssets !== 'object') track.textureAssets = {}; return track.textureAssets; }
function textureAssetEntries() { return Object.entries(textureAssets()); }
function textureGrid(asset) {
  const cols = Math.floor(asset.width / asset.tileWidth);
  const rows = Math.floor(asset.height / asset.tileHeight);
  return { cols, rows, count: cols * rows };
}
function uniqueTextureAssetId(filename) {
  return TrackMesh.uniqueAssetId(filename || 'texture', new Set(Object.keys(textureAssets())));
}
function clampTextureTileSize(asset, key, value) {
  const max = key === 'tileWidth' ? asset.width : asset.height;
  return Math.max(1, Math.min(max, Math.floor(Number(value) || max)));
}
function clearInvalidTextureAssignments(assetId) {
  const asset = textureAssets()[assetId];
  if (!asset) return;
  const count = textureGrid(asset).count;
  for (const path of track.paths || []) {
    if (path.texture && path.texture.asset === assetId && path.texture.tile >= count) path.texture = null;
  }
}
function currentCurve() { return track.paths && track.paths[sel.path] ? track.paths[sel.path] : null; }
function assignCurrentCurveTexture(assetId, tile) {
  const path = currentCurve();
  if (!path) return;
  if (path.texture && path.texture.asset === assetId && path.texture.tile === tile) return;
  pushUndo();
  path.texture = { asset: assetId, tile };
  refresh();
}
function clearCurrentCurveTexture() {
  const path = currentCurve();
  if (!path || !path.texture) return;
  pushUndo();
  path.texture = null;
  refresh();
}
function deleteTextureAsset(assetId) {
  const asset = textureAssets()[assetId];
  if (!asset) return;
  if (!confirm(`Delete texture image "${asset.name}" and clear curves using it?`)) return;
  pushUndo();
  delete textureAssets()[assetId];
  for (const path of track.paths || []) if (path.texture && path.texture.asset === assetId) path.texture = null;
  refresh();
}
function imageSizeFromDataUrl(dataUrl) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve({ width: img.naturalWidth || img.width, height: img.naturalHeight || img.height });
    img.onerror = () => reject(new Error('image could not be decoded'));
    img.src = dataUrl;
  });
}
function readFileAsDataUrl(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result || ''));
    reader.onerror = () => reject(reader.error || new Error('could not read file'));
    reader.readAsDataURL(file);
  });
}
async function loadTextureImageFile(file) {
  try {
    const dataUrl = await readFileAsDataUrl(file);
    const size = await imageSizeFromDataUrl(dataUrl);
    pushUndo();
    const id = uniqueTextureAssetId(file.name);
    textureAssets()[id] = { name: file.name || id, dataUrl, width: size.width, height: size.height, tileWidth: size.width, tileHeight: size.height };
    texturePanelOpen = true;
    refresh();
  } catch (err) {
    alert('Could not load image: ' + (err.message || err));
  }
}
function renderTexturePanel() {
  const panel = document.getElementById('texturePanel');
  if (!panel) return;
  panel.style.display = texturePanelOpen ? 'block' : 'none';
  if (!texturePanelOpen) return;
  const list = document.getElementById('textureAssetList');
  const cur = currentCurve();
  const selected = cur && cur.texture;
  const entries = textureAssetEntries();
  if (!entries.length) {
    list.innerHTML = '<div class="hint" style="margin-top:10px">No texture images loaded.</div>';
    return;
  }
  list.innerHTML = entries.map(([id, asset]) => {
    const { cols, rows, count } = textureGrid(asset);
    const tiles = Array.from({ length: count }, (_, tile) => {
      const x = tile % cols, y = Math.floor(tile / cols);
      const selectedClass = selected && selected.asset === id && selected.tile === tile ? ' selected' : '';
      const bgSize = `${asset.width / asset.tileWidth * 48}px ${asset.height / asset.tileHeight * 48}px`;
      const bgPos = `-${x * 48}px -${y * 48}px`;
      return `<button class="tile${selectedClass}" data-asset="${id}" data-tile="${tile}" title="Texture ${tile}" style="background-image:url(${asset.dataUrl});background-size:${bgSize};background-position:${bgPos}"></button>`;
    }).join('');
    return `<div class="asset" data-asset="${id}">
      <div class="assetTitle"><span class="name">${escapeHtml(asset.name)}</span><span class="spacer"></span><button data-action="deleteTexture" data-asset="${id}">Delete</button></div>
      <div class="assetMeta">Image: ${asset.width} × ${asset.height} px<br>Textures: ${count} (${cols} × ${rows})</div>
      <label>Tile W <input type="number" min="1" max="${asset.width}" value="${asset.tileWidth}" data-action="tileSize" data-asset="${id}" data-key="tileWidth"></label>
      <label>Tile H <input type="number" min="1" max="${asset.height}" value="${asset.tileHeight}" data-action="tileSize" data-asset="${id}" data-key="tileHeight"></label>
      <div class="tiles">${tiles}</div>
    </div>`;
  }).join('');
}

// Hit-test a world point against placements, nearest-last so the topmost drawn
// region wins the click.
function meshAtWorld(wx, wz) {
  const all = compiledMeshes();
  for (let i = all.length - 1; i >= 0; i--) {
    if (TrackMesh.containsWorldPoint(all[i].compiled, wx, wz)) return all[i];
  }
  return null;
}

// Nearest mesh edge to a world point, within a screen-space tolerance.
function meshEdgeAtWorld(wx, wz, tolWorld) {
  let best = null;
  for (const { placement, mesh } of compiledMeshes()) {
    for (const seg of TrackMesh.edgeSegments(mesh, placement)) {
      const c = TrackMesh.closestPointOnSegment(seg.a.x, seg.a.z, seg.b.x, seg.b.z, wx, wz);
      const d2 = (wx - c.x) ** 2 + (wz - c.z) ** 2;
      if (d2 <= tolWorld * tolWorld && (!best || d2 < best.d2)) {
        best = { meshId: placement.id, assetId: placement.asset, edgeId: seg.edgeId, d2 };
      }
    }
  }
  return best;
}

function newMeshPlacementId() {
  const taken = new Set(meshPlacements().map(m => m.id));
  for (let i = 1; ; i++) if (!taken.has('m' + i)) return 'm' + i;
}

// Parse geometry-js mesh JSON. Returns { mesh } or { error }, never throws and
// never touches the track -- so callers can report a bad paste without having
// already half-mutated state.
function parseMeshJSON(text) {
  if (!String(text || '').trim()) return { error: 'nothing to import (the clipboard is empty)' };
  let data;
  try { data = JSON.parse(text); } catch (err) { return { error: 'not valid JSON (' + err.message + ')' }; }
  const wrapped = TrackCore.normalizeMeshAssets({ probe: data });
  if (!wrapped.probe) return { error: 'no vertices/polygons array found -- is this a geometry-js mesh?' };
  try { return { mesh: TrackMesh.meshFromJSON(wrapped.probe.mesh) }; }
  catch (err) { return { error: err.message }; }
}

// Import a parsed mesh as a new asset, always under a fresh id so an existing
// placement is never disturbed by a re-import, and drop one placement of it.
// `at` is the world position for the placement origin; omit it to centre the
// shape on the current view.
function addMeshAsset(mesh, name, at) {
  // Enclosed by default: an imported region is walled all the way round, so
  // it is drivable the moment it lands. Opening a ledge is one click in Rails
  // mode, whereas the other default -- a bare rim -- makes every new region a
  // pad you slide straight off before you can do anything about it.
  TrackMesh.railBoundaryEdges(mesh);

  pushUndo();
  if (!track.meshAssets) track.meshAssets = {};
  if (!track.meshes) track.meshes = [];
  const assetId = TrackMesh.uniqueAssetId(name, new Set(Object.keys(track.meshAssets)));
  track.meshAssets[assetId] = {
    name: assetId,
    railHeight: TrackCore.DEFAULT_RAIL_HEIGHT,
    mesh: TrackMesh.meshToJSON(mesh)
  };
  let x = 0, z = 0;
  if (!at) {
    // Centre the shape on the view rather than its own origin, so an asset
    // authored far from (0,0) still lands where you can see it.
    const bounds = TrackMesh.assetBounds(mesh);
    const centre = screenToWorld(view.w / 2, view.h / 2);
    x = Math.round((centre.x - bounds.cx) * 10) / 10;
    z = Math.round((centre.z - bounds.cy) * 10) / 10;
  } else { x = at.x; z = at.z; }
  const placement = { id: newMeshPlacementId(), asset: assetId, x, z, rotation: 0, elevation: 0 };
  track.meshes.push(placement);
  invalidateMeshCache();
  selectedMeshId = placement.id;
  railSel = null;
  updateUndoRedoButtons();
  refresh();
}

function importMeshFile(filename, text) {
  const { mesh, error } = parseMeshJSON(text);
  if (error) { alert('Mesh import failed: ' + error); return; }
  addMeshAsset(mesh, filename);
}

// Paste a mesh copied from the geometry-js editor's "Copy JSON" button.
// `centreOn` centres the shape's bounds on that world point (used by the
// right-click menu, where the click position is the whole point). Without it
// the region goes to the world origin, honouring its authored coordinates.
async function importMeshFromClipboard(centreOn) {
  let text;
  try {
    if (!navigator.clipboard?.readText) throw new Error('clipboard reads are unavailable in this browser (needs https:// or localhost)');
    text = await navigator.clipboard.readText();
  } catch (err) {
    alert('Could not read the clipboard: ' + err.message +
      '\n\nUse Import Mesh to load a .json file instead.');
    return;
  }
  const { mesh, error } = parseMeshJSON(text);
  if (error) { alert('Clipboard does not contain a mesh: ' + error); return; }
  let at = { x: 0, z: 0 };
  if (centreOn && Number.isFinite(centreOn.x) && Number.isFinite(centreOn.z)) {
    // Offset by the shape's own centre so it lands *centred* on the click
    // rather than hanging off it by however far the asset was authored from
    // its own origin.
    const bounds = TrackMesh.assetBounds(mesh);
    at = {
      x: Math.round((centreOn.x - bounds.cx) * 10) / 10,
      z: Math.round((centreOn.z - bounds.cy) * 10) / 10
    };
  }
  addMeshAsset(mesh, 'pasted-mesh', at);
}

function deleteSelectedMesh() {
  const placement = selectedPlacement();
  if (!placement) return;
  pushUndo();
  track.meshes = meshPlacements().filter(m => m.id !== placement.id);
  clearMeshSelection();
  invalidateMeshCache();
  updateUndoRedoButtons();
  refresh();
}
function branchPointIdsForPaths(paths, junctions) {
  const ids = new Set((junctions || []).map(j => j.pointId).filter(Boolean));
  const stats = new Map();
  const stat = id => {
    if (!stats.has(id)) stats.set(id, { endpoints: 0, interior: 0, closed: 0 });
    return stats.get(id);
  };
  for (const path of paths || []) {
    const cps = parts(path).controlPoints;
    const closed = path.closed !== false;
    for (let i = 0; i < cps.length; i++) {
      const p = cps[i];
      if (!p || !p.id) continue;
      const s = stat(p.id);
      if (closed) s.closed++;
      else if (i === 0 || i === cps.length - 1) s.endpoints++;
      else s.interior++;
    }
  }
  for (const [id, s] of stats) {
    if (s.endpoints >= 3) ids.add(id);
    else if (s.endpoints >= 1 && (s.closed > 0 || s.interior > 0)) ids.add(id);
  }
  return ids;
}
function syncSelectionToId() {
  if (!selectedPointId) {
    const first = track.paths[0] && parts(track.paths[0]).controlPoints[0];
    selectedPointId = first && first.id;
  }
  for (let pi = 0; pi < track.paths.length; pi++) {
    const cps = parts(track.paths[pi]).controlPoints;
    const idx = cps.findIndex(p => p.id === selectedPointId);
    if (idx >= 0) { sel = { path: pi, point: idx }; return; }
  }
  const fallback = track.paths[0] && parts(track.paths[0]).controlPoints[0];
  selectedPointId = fallback && fallback.id;
  sel = { path: 0, point: 0 };
}
function selectPosition(pathIndex, pointIndex) {
  const p = track.paths[pathIndex] && parts(track.paths[pathIndex]).controlPoints[pointIndex];
  if (p) selectedPointId = p.id;
  // Path points and mesh regions share one props panel, so selecting either
  // clears the other.
  clearMeshSelection();
  syncSelectionToId();
}
function curPath() { syncSelectionToId(); return track.paths[sel.path]; }
function curParts() { const p = curPath(); return p ? parts(p) : { controlPoints: [], rollPoints: [], widthPoints: [] }; }
function curPoint() { syncSelectionToId(); return curParts().controlPoints[sel.point]; }

// ---------- Structural helpers on the unified `points` array ----------
// Array indices (into path.points) of the 'position'-type entries, in order
// -- that order IS the path's shape sequence.
function positionIndices(path) {
  const idxs = [];
  path.points.forEach((p, i) => { if (p.type === 'position') idxs.push(i); });
  return idxs;
}
// Insert a position-type point so it becomes the k-th position point.
function insertPositionAt(path, k, obj) {
  const idxs = positionIndices(path);
  const arrIdx = k < idxs.length ? idxs[k] : path.points.length;
  path.points.splice(arrIdx, 0, obj);
}
// Flat, straight-sided defaults for a path that has no authored roll/width/
// cross-section of its own -- a curve drawn in Create mode, or either half of a
// segment split.
//
// The width MUST come from TrackCore, not a literal: it is the one value here
// that is a world LENGTH, so it moves with the unit scale. Schema 5 doubled the
// world and this function kept its old hardcoded 12, which is why every curve
// drawn in the editor came out half the width of an imported one -- and why
// splitting a path silently halved both halves. Everything else below is
// scale-invariant (roll is an angle, curvature dimensionless, tightness an
// exponent) and stays as written. Thickness IS a length, which is why the
// cross-section points below are built through crossSectionPoint().

/* Build a cross-section point. Everything that mints one goes through here.
 *
 * These points started with one value, gained `tightness`, and have now gained
 * `thickness` -- and the editor constructs them in a dozen places (splits,
 * joins, seam reconnects, type conversions, insertions). Spelling the fields out
 * at each of those is how a new field gets silently dropped: a split would quietly
 * reset every road's thickness to the default. One builder means the next field
 * is added once. Same reasoning as uniqueScalarPoints replacing whole points
 * rather than copying one named key. */
function crossSectionPoint(t, curvature, tightness, thickness) {
  return {
    type: 'crossSection', t,
    curvature: curvature == null ? 0 : curvature,
    tightness: tightness == null ? 1 : tightness,
    thickness: thickness == null ? TrackCore.DEFAULT_CROSS_SECTION_THICKNESS : thickness
  };
}
// Copy an existing cross-section point to a new t, carrying every value across.
const crossSectionCopy = (t, src) => crossSectionPoint(t, src.curvature, src.tightness, src.thickness);

function flatRollWidthDefaults(closed = true) {
  const endT = closed ? 0.5 : 1;
  const width = TrackCore.DEFAULT_WIDTH;
  return [
    { type: 'roll', t: 0, roll: 0 }, { type: 'roll', t: endT, roll: 0 },
    { type: 'width', t: 0, width }, { type: 'width', t: endT, width },
    crossSectionPoint(0, 0, 1), crossSectionPoint(endT, 0, 1)
  ];
}
function zeroElevationAndRoll(t) {
  for (const path of t.paths || []) {
    for (const p of path.points || []) {
      if (p.type === 'position') p.pos[1] = 0;
      else if (p.type === 'roll') p.roll = 0;
    }
  }
}
let nextGeneratedId = 1;
let usedIds = new Set();
function newId(prefix) {
  let id;
  do { id = prefix + (nextGeneratedId++); } while (usedIds.has(id));
  usedIds.add(id);
  return id;
}
function ensureTrackIds() {
  if (!track.disjointSeams) track.disjointSeams = [];
  if (!track.junctions) track.junctions = [];
  if (!track.selfIntersectionOverrides) track.selfIntersectionOverrides = [];
  if (!track.meshAssets) track.meshAssets = {};
  if (!track.meshes) track.meshes = [];
  // Cloned built-ins / hand-built tracks may lack a handling section; fill and
  // clamp it so the editor always has a complete one to show and serialize.
  track.handling = TrackCore.normalizeHandling(track.handling);
  usedIds = new Set((track.disjointSeams || []).concat(track.junctions || []).map(s => s.id).filter(Boolean));
  const pointById = new Map(), pathIds = new Set();
  for (const path of track.paths) {
    if (!path.id || pathIds.has(path.id) || usedIds.has(path.id)) path.id = newId('path');
    pathIds.add(path.id); usedIds.add(path.id);
    for (let i = 0; i < path.points.length; i++) {
      const p = path.points[i];
      if (p.type !== 'position') continue;
      if (!p.id) p.id = newId('p');
      if (pointById.has(p.id)) path.points[i] = pointById.get(p.id);
      else { pointById.set(p.id, p); usedIds.add(p.id); }
    }
  }
}
ensureTrackIds();

// Keep track.start's indices valid after paths are added/removed/resized by
// editing operations. This only clamps bounds -- it doesn't try to track the
// "same" control point through a restructure (e.g. a segment delete that
// rotates a closed path's point order), so a structural edit near the start
// point may leave it pointing at a nearby-but-different point; re-set it with
// "Set as Start" if that happens.
function clampStart() {
  if (!track.start) track.start = { path: 0, point: 0, reverse: false };
  track.start.path = Math.max(0, Math.min(track.paths.length - 1, track.start.path));
  const n = parts(track.paths[track.start.path]).controlPoints.length;
  track.start.point = Math.max(0, Math.min(n - 1, track.start.point));
}

// Exact analytic frame (pos + tangent) at the start control point, via the
// same rational evaluator the game uses -- g = the control point's own index
// is a valid domain value for both closed and open paths.
function startFrame() {
  clampStart();
  const path = track.paths[track.start.path];
  const pp = parts(path);
  const { evalTrack } = TrackCore.makeEvaluator(pp.controlPoints, path.closed, pp.rollPoints, pp.widthPoints, pp.crossSectionPoints);
  return evalTrack(track.start.point);
}

// Heuristic screen-space winding of a closed path's centerline (shoelace on
// world X/Z, which worldToScreen maps 1:1 with no axis flip). Used only to
// label the direction button; the actual start heading is unaffected.
function pathIsClockwise(path) {
  const { evalTrack, CP_N } = TrackCore.makeEvaluator(parts(path).controlPoints, true);
  const M = 64;
  let area = 0, prev = evalTrack(0).pos;
  for (let i = 1; i <= M; i++) {
    const cur = evalTrack((i / M) * CP_N % CP_N).pos;
    area += prev.x * cur.z - cur.x * prev.z;
    prev = cur;
  }
  return area > 0;
}
function updateDirBtn() {
  const btn = document.getElementById('dirBtn');
  const path = track.paths[track.start.path];
  if (path.closed) {
    const cw = pathIsClockwise(path) !== !!track.start.reverse;
    btn.textContent = 'Direction: ' + (cw ? 'Clockwise' : 'Anticlockwise');
  } else {
    btn.textContent = 'Direction: ' + (track.start.reverse ? 'Reversed' : 'Forward');
  }
}

const topCanvas = document.getElementById('topCanvas');
const elevCanvas = document.getElementById('elevCanvas');
const topCtx = topCanvas.getContext('2d');
const elevCtx = elevCanvas.getContext('2d');

// ---------- Canvas sizing (device-pixel aware) ----------
function fitCanvas(canvas, ctx) {
  const dpr = window.devicePixelRatio || 1;
  const r = canvas.getBoundingClientRect();
  canvas.width = Math.max(1, Math.round(r.width * dpr));
  canvas.height = Math.max(1, Math.round(r.height * dpr));
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { w: r.width, h: r.height };
}

// ---------- World <-> screen mapping for the top-down view -------------------
// Auto-fit the control-point bounds (with margin) into the canvas, preserving
// aspect ratio. X -> screen x, Z -> screen y (top-down looking down -Y).
let view = { scale: 1, ox: 0, oy: 0, w: 1, h: 1 };
let frozenViewBounds = null; // keep top-down zoom stable during point drags
function computeTrackBounds() {
  let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
  for (const path of track.paths) {
    for (const p of parts(path).controlPoints) {
      minX = Math.min(minX, p.pos[0]); maxX = Math.max(maxX, p.pos[0]);
      minZ = Math.min(minZ, p.pos[2]); maxZ = Math.max(maxZ, p.pos[2]);
    }
  }
  for (const { compiled } of compiledMeshes()) {
    minX = Math.min(minX, compiled.bounds.minX); maxX = Math.max(maxX, compiled.bounds.maxX);
    minZ = Math.min(minZ, compiled.bounds.minZ); maxZ = Math.max(maxZ, compiled.bounds.maxZ);
  }
  if (!isFinite(minX)) return { minX: -1, maxX: 1, minZ: -1, maxZ: 1 };
  return { minX, maxX, minZ, maxZ };
}
function freezeTopViewForDrag() { if (!frozenViewBounds) frozenViewBounds = computeTrackBounds(); }
function releaseTopViewFreeze() {
  if (!frozenViewBounds) return;
  const oldScale = view.scale, center = screenToWorld(view.w / 2, view.h / 2);
  frozenViewBounds = null;
  const { minX, maxX, minZ, maxZ } = computeTrackBounds();
  const margin = 30;
  const spanX = (maxX - minX) || 1, spanZ = (maxZ - minZ) || 1;
  const baseScale = Math.min((view.w - 2 * margin) / spanX, (view.h - 2 * margin) / spanZ) || 1;
  topZoom = oldScale / baseScale;
  const slider = document.getElementById('topZoomSlider');
  if (slider) slider.value = Math.max(-100, Math.min(100, Math.round(Math.log2(topZoom) * 50)));
  const cx = (minX + maxX) / 2, cz = (minZ + maxZ) / 2;
  topPan.x = (cx - center.x) * oldScale;
  topPan.y = (cz - center.z) * oldScale;
}
function computeView(w, h) {
  const { minX, maxX, minZ, maxZ } = frozenViewBounds || computeTrackBounds();
  const margin = 30;
  const spanX = (maxX - minX) || 1, spanZ = (maxZ - minZ) || 1;
  const scale = Math.min((w - 2 * margin) / spanX, (h - 2 * margin) / spanZ) * topZoom;
  const cx = (minX + maxX) / 2, cz = (minZ + maxZ) / 2;
  view = { scale, ox: w / 2 - cx * scale + topPan.x, oy: h / 2 - cz * scale + topPan.y, w, h };
}
const worldToScreen = (x, z) => ({ x: x * view.scale + view.ox, y: z * view.scale + view.oy });
const screenToWorld = (sx, sy) => ({ x: (sx - view.ox) / view.scale, z: (sy - view.oy) / view.scale });
function snapWorldXZ(w) {
  if (!snapToGrid) return w;
  return {
    x: Math.round(w.x / gridSize) * gridSize,
    z: Math.round(w.z / gridSize) * gridSize
  };
}

// ---------- Height color (shared by node shading and roll tint) --------------
function heightColor(y) {
  // blue (low) -> teal -> warm (high)
  const t = Math.max(-1, Math.min(1, y / 8));
  const r = Math.round(60 + 150 * Math.max(0, t));
  const g = Math.round(150 + 60 * (1 - Math.abs(t)));
  const b = Math.round(180 - 120 * Math.max(0, t) + 40 * Math.max(0, -t));
  return `rgb(${r},${g},${b})`;
}
function rollTint(rollDeg) {
  // right-lean (negative) -> cyan, left-lean (positive) -> magenta-ish
  const t = Math.max(-1, Math.min(1, rollDeg / 25));
  const r = Math.round(120 + 120 * Math.max(0, t));
  const g = Math.round(150);
  const b = Math.round(120 + 120 * Math.max(0, -t));
  return `rgb(${r},${g},${b})`;
}
// Green (0 deg) -> red (+-180 deg), symmetric about 0: [0,180] interpolates
// green->red, [-180,0] interpolates red->green. Used for 'flat' render mode,
// where the ribbon itself carries the roll cue instead of the edge geometry.
function rollColor(rollDeg) {
  const t = Math.max(0, Math.min(1, Math.abs(rollDeg) / 180));
  const r = Math.round(40 + (210 - 40) * t);
  const g = Math.round(190 + (50 - 190) * t);
  const b = 55;
  return `rgb(${r},${g},${b})`;
}
function elevationColor(y, minY, maxY) {
  const t = Math.max(0, Math.min(1, (y - minY) / ((maxY - minY) || 1)));
  const r = Math.round(40 + (220 - 40) * t);
  const g = Math.round(210 + (55 - 210) * t);
  const b = 55;
  return `rgb(${r},${g},${b})`;
}

// Per-path baked preview data, cached each draw so hit-testing (segments,
// nodes) can reuse it without re-evaluating the spline.
let pathPreviews = [];
// Per-path list of self-intersections in WORLD space ({side,a,b,span,world}),
// cached so the O(N^2) detection runs only on idle redraws, not every frame of
// a drag/pan. Re-projected to screen and re-coloured (by current overrides)
// cheaply on every draw. Parallel to pathPreviews.
let crossingCache = [];

// ---------- self-intersection crossing markers ----------
// The saved override (if any) for a crossing, matched by side + unordered id
// pair.
function crossingOverrideFor(a, b, side) {
  return (track.selfIntersectionOverrides || []).find(o =>
    o.side === side && ((o.a === a && o.b === b) || (o.a === b && o.b === a)));
}
// Effective state of a crossing: forced-* when an override exists, else auto-*
// per the default local-window rule (mirrors TrackCore's default decide).
function crossingState(cr) {
  const o = crossingOverrideFor(cr.a, cr.b, cr.side);
  if (o) return o.action === 'collapse' ? 'forced-collapse' : 'forced-keep';
  return cr.span <= TrackCore.DEFAULT_SELF_INTERSECTION_SPAN ? 'auto-collapse' : 'auto-keep';
}
const CROSSING_COLORS = {
  'auto-collapse': '#b9c2d0', // light grey: removed automatically
  'auto-keep': '#ffb020',     // amber: kept automatically (far crossing) -- the
                              // "overlap still there" case; deliberately NOT the
                              // cyan of the centerline so it stands out on top
  'forced-collapse': '#ff3355',// red: user forced removal
  'forced-keep': '#37d17a'    // green: user forced keep
};
const CROSSING_HIT_RADIUS = 11;
// Detect every self-intersection on a path's two edges (pre-collapse), keyed by
// the control-point-id pair so it survives edits/resampling. World-space.
function detectPathCrossings(controlPoints, closed, edges, wrapOpen) {
  const out = [];
  const scan = (side, pts) => {
    for (const c of TrackCore.findSelfIntersections(pts, closed, wrapOpen)) {
      const key = TrackCore.crossingKey(controlPoints, closed, TrackCore.N_DEFAULT, c);
      if (key[0] == null || key[1] == null) continue;
      out.push({ side, a: key[0], b: key[1], span: c.span, world: { x: c.point.x, z: c.point.z } });
    }
  };
  scan('left', edges.left);
  scan('right', edges.right);
  return out;
}
// Nearest crossing marker to a screen point, or null. Searches the cache.
function crossingMarkerAtTop(sx, sy) {
  let best = null, bestD = CROSSING_HIT_RADIUS;
  for (const list of crossingCache) {
    for (const cr of list || []) {
      const s = worldToScreen(cr.world.x, cr.world.z);
      const d = Math.hypot(s.x - sx, s.y - sy);
      if (d <= bestD) { bestD = d; best = cr; }
    }
  }
  return best;
}
// Cycle a crossing's override: auto -> keep -> collapse -> auto. Undo-able.
function cycleCrossingOverride(cr) {
  pushUndo();
  if (!track.selfIntersectionOverrides) track.selfIntersectionOverrides = [];
  const list = track.selfIntersectionOverrides;
  const idx = list.findIndex(o =>
    o.side === cr.side && ((o.a === cr.a && o.b === cr.b) || (o.a === cr.b && o.b === cr.a)));
  if (idx < 0) list.push({ side: cr.side, a: cr.a, b: cr.b, action: 'keep' });
  else if (list[idx].action === 'keep') list[idx].action = 'collapse';
  else list.splice(idx, 1);
  refresh();
}

// ---------- Draw the top-down ribbon(s) ----------
// Mesh regions in the top-down view. Drawn in every render mode (they are
// track surface regardless of how the ribbon is being visualised): a tinted
// fill for the drivable area, railed edges as thick solid walls, bare edges as
// dashed ledges, and in Rails mode every edge is highlighted as a click target.
function drawMeshes(ctx) {
  const railsMode = editMode === 'rails';
  const tracePath = (loop) => {
    ctx.beginPath();
    loop.forEach((p, i) => {
      const s = worldToScreen(p.x, p.z);
      if (i === 0) ctx.moveTo(s.x, s.y); else ctx.lineTo(s.x, s.y);
    });
    ctx.closePath();
  };

  for (const { placement, mesh, compiled } of compiledMeshes()) {
    const selected = placement.id === selectedMeshId;

    // Fill drivable area, punching out holes via even-odd winding.
    ctx.beginPath();
    for (const poly of compiled.polygons) {
      poly.outer.forEach((p, i) => {
        const s = worldToScreen(p.x, p.z);
        if (i === 0) ctx.moveTo(s.x, s.y); else ctx.lineTo(s.x, s.y);
      });
      ctx.closePath();
      for (const hole of poly.holes) {
        hole.forEach((p, i) => {
          const s = worldToScreen(p.x, p.z);
          if (i === 0) ctx.moveTo(s.x, s.y); else ctx.lineTo(s.x, s.y);
        });
        ctx.closePath();
      }
    }
    ctx.fillStyle = selected ? 'rgba(150,110,220,0.34)' : 'rgba(120,90,180,0.22)';
    ctx.fill('evenodd');

    // Show the triangulation the game will actually render, so a bad import is
    // visible here rather than only once you drive on it.
    if (selected) {
      ctx.strokeStyle = 'rgba(190,160,255,0.22)';
      ctx.lineWidth = 1;
      for (const tri of compiled.triangles) { tracePath(tri); ctx.stroke(); }
    }

    for (const seg of TrackMesh.edgeSegments(mesh, placement)) {
      const a = worldToScreen(seg.a.x, seg.a.z);
      const b = worldToScreen(seg.b.x, seg.b.z);
      const isRailSel = railSel && railSel.meshId === placement.id && railSel.edgeId === seg.edgeId;
      ctx.beginPath();
      ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y);
      if (seg.rail) {
        ctx.setLineDash([]);
        ctx.strokeStyle = isRailSel ? '#fff2a8' : '#d8b400';
        ctx.lineWidth = isRailSel ? 5 : 3.5;
      } else {
        ctx.setLineDash([5, 4]);
        ctx.strokeStyle = railsMode ? 'rgba(210,180,255,0.85)' : 'rgba(180,150,230,0.55)';
        ctx.lineWidth = isRailSel ? 3 : 1.5;
      }
      ctx.stroke();
      ctx.setLineDash([]);
    }

    if (selected) {
      ctx.strokeStyle = '#e6d8ff';
      ctx.lineWidth = 1.5;
      for (const poly of compiled.polygons) { tracePath(poly.outer); ctx.stroke(); }
      // Origin handle doubles as a rotation readout anchor.
      const o = worldToScreen(placement.x, placement.z);
      ctx.fillStyle = '#e6d8ff';
      ctx.beginPath(); ctx.arc(o.x, o.y, 4, 0, Math.PI * 2); ctx.fill();
      ctx.font = '11px system-ui';
      ctx.fillText(`${placement.asset}  y ${placement.elevation.toFixed(1)}  ${placement.rotation.toFixed(0)}°`, o.x + 8, o.y - 6);
    }
  }
}

function drawTop() {
  const { w, h } = fitCanvas(topCanvas, topCtx);
  computeView(w, h);
  const ctx = topCtx;
  ctx.clearRect(0, 0, w, h);

  // faint grid
  ctx.strokeStyle = 'rgba(40,70,95,0.35)';
  ctx.lineWidth = 1;
  const step = gridSize * view.scale;
  if (step > 6) {
    for (let gx = view.ox % step; gx < w; gx += step) { ctx.beginPath(); ctx.moveTo(gx, 0); ctx.lineTo(gx, h); ctx.stroke(); }
    for (let gy = view.oy % step; gy < h; gy += step) { ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(w, gy); ctx.stroke(); }
  }

  // Mesh regions draw beneath the paths: they are backdrop surfaces, and path
  // control points must stay visible and clickable on top of them.
  drawMeshes(ctx);

  const flat = renderMode === 'flat' || renderMode === 'elevation';
  const elevationMode = renderMode === 'elevation';
  const branchPointIds = branchPointIdsForPaths(track.paths, track.junctions || []);
  const bakedPaths = track.paths.map(path => {
    const pp = parts(path);
    // Use the SAME baked frames the game uses, so the preview matches exactly
    // what you drive. Edges are either the real banked geometry, or (flat
    // mode) the unrolled width-only footprint. Paths sharing a control point
    // just meet/overlap there -- EXCEPT disjoint seams (hard corners), whose
    // edges get mitred below, matching track.html's buildTrack().
    const closed = path.closed !== false;
    const frames = TrackCore.buildCenterline(pp.controlPoints, TrackCore.N_DEFAULT, closed, pp.rollPoints, pp.widthPoints, pp.crossSectionPoints);
    const edges = flat ? TrackCore.buildFlatEdges(frames, closed) : TrackCore.buildEdges(frames, closed);
    const hasBranchConnection = pp.controlPoints.some(cp => cp && branchPointIds.has(cp.id));
    return { id: path.id, closed, parts: pp, controlPoints: pp.controlPoints, frames, edges, hasBranchConnection };
  });
  const edgeCuts = TrackCore.computeDisjointEdgeCuts(bakedPaths, track.disjointSeams || []);
  if (!dragging) crossingCache = []; // rebuilt per-path below; stays cached during drags
  pathPreviews = bakedPaths.map((bp, pathIndex) => {
    const pp = bp.parts, frames = bp.frames;
    let edges = bp.edges;
    const cuts = edgeCuts[pathIndex] || {};
    if (cuts.start) {
      if (cuts.start.left) edges.left[0] = cuts.start.left;
      if (cuts.start.right) edges.right[0] = cuts.start.right;
    }
    if (cuts.end) {
      const i = frames.length - 1;
      if (cuts.end.left) edges.left[i] = cuts.end.left;
      if (cuts.end.right) edges.right[i] = cuts.end.right;
    }
    const wrapsAtDisjointSeam = !bp.closed && !!cuts.start && !!cuts.end &&
      bp.controlPoints[0] && bp.controlPoints[bp.controlPoints.length - 1] && bp.controlPoints[0].id === bp.controlPoints[bp.controlPoints.length - 1].id;
    // Detect self-intersections on the pre-collapse edges (for clickable
    // markers). Heavy (O(N^2)); only when idle -- during a drag/pan reuse the
    // cached world-space crossings so the frame stays cheap. Branch-connected
    // curves intentionally skip self-intersection detection/cleanup so their
    // authored geometry is not altered by branch handling.
    if (!dragging) crossingCache[pathIndex] = bp.hasBranchConnection ? [] : detectPathCrossings(bp.controlPoints, bp.closed, edges, wrapsAtDisjointSeam);

    if (!bp.hasBranchConnection) {
      // Self-intersections (a tight fold, or the SAME side of this SAME curve
      // crossing itself along its length -- e.g. a wiggly chicane) are collapsed
      // to the crossing point here, on the edges shared by the road fill AND the
      // wall outline below. This matches the game (track-game.js buildPath), which
      // likewise cleans the edges that feed both its road ribbon and its physics
      // corridor -- so the editor preview shows exactly what the game builds.
      // Per-crossing keep/collapse overrides (authored via the markers) flow
      // through the same deciders the game uses.
      const deciders = TrackCore.makeSelfIntersectionDeciders(bp.controlPoints, bp.closed, TrackCore.N_DEFAULT, track.selfIntersectionOverrides || []);
      edges = TrackCore.removeLocalEdgeSelfIntersections(
        edges, bp.closed, wrapsAtDisjointSeam,
        deciders && deciders.decideLeft, deciders && deciders.decideRight, deciders && deciders.scanSpan
      );
    }
    const count = bp.closed ? frames.length + 1 : frames.length; // closed: echo sample 0 at the end
    const centerPts = [], leftPts = [], rightPts = [], rollAt = [], yAt = [];
    for (let i = 0; i < count; i++) {
      const j = i % frames.length;
      const f = frames[j];
      centerPts.push(worldToScreen(f.pos.x, f.pos.z));
      leftPts.push(worldToScreen(edges.left[j].x, edges.left[j].z));
      rightPts.push(worldToScreen(edges.right[j].x, edges.right[j].z));
      rollAt.push(f.roll * 180 / Math.PI);
      yAt.push(f.pos.y);
    }
    return { parts: pp, frames, edges, centerPts, leftPts, rightPts, rollAt, yAt };
  });

  const allY = pathPreviews.flatMap(p => p.yAt);
  const minElev = Math.min(...allY), maxElev = Math.max(...allY);

  for (const prev of pathPreviews) {
    const { leftPts, rightPts, rollAt, centerPts, yAt } = prev;

    // --- road fill ---
    if (flat) {
      // ribbon filled per-segment, coloured by interpolated roll (the edge
      // geometry carries no banking cue in this mode, so colour does instead).
      for (let i = 0; i < leftPts.length - 1; i++) {
        ctx.beginPath();
        ctx.moveTo(leftPts[i].x, leftPts[i].y);
        ctx.lineTo(leftPts[i + 1].x, leftPts[i + 1].y);
        ctx.lineTo(rightPts[i + 1].x, rightPts[i + 1].y);
        ctx.lineTo(rightPts[i].x, rightPts[i].y);
        ctx.closePath();
        ctx.fillStyle = elevationMode
          ? elevationColor((yAt[i] + yAt[i + 1]) / 2, minElev, maxElev)
          : rollColor(rollAt[i]);
        ctx.fill();
      }
    } else {
      // filled ribbon
      ctx.beginPath();
      ctx.moveTo(leftPts[0].x, leftPts[0].y);
      for (let i = 1; i < leftPts.length; i++) ctx.lineTo(leftPts[i].x, leftPts[i].y);
      for (let i = rightPts.length - 1; i >= 0; i--) ctx.lineTo(rightPts[i].x, rightPts[i].y);
      ctx.closePath();
      ctx.fillStyle = 'rgba(42,55,75,0.55)';
      ctx.fill();
    }
    // --- guard rail edges: the post-cleanup left/right curve edges.
    ctx.strokeStyle = '#ffdd00'; ctx.lineWidth = 2;
    for (const edge of [leftPts, rightPts]) {
      ctx.beginPath(); ctx.moveTo(edge[0].x, edge[0].y);
      for (let i = 1; i < edge.length; i++) ctx.lineTo(edge[i].x, edge[i].y);
      ctx.stroke();
    }
    // centerline
    ctx.strokeStyle = 'rgba(79,214,255,0.9)';
    ctx.setLineDash([6, 5]); ctx.lineWidth = 1.5;
    ctx.beginPath(); ctx.moveTo(centerPts[0].x, centerPts[0].y);
    for (let i = 1; i < centerPts.length; i++) ctx.lineTo(centerPts[i].x, centerPts[i].y);
    ctx.stroke(); ctx.setLineDash([]);
  }

  // Physics sample points: the N_DEFAULT uniform reference frames the editor
  // previews with, one small dot per frame per path (the game samples the same
  // curve adaptively by length -- see showPhysicsPoints). Drawn on top of the
  // ribbon but beneath the authored control-point handles so editing is never
  // obstructed.
  if (showPhysicsPoints) {
    pathPreviews.forEach((prev, pi) => {
      prev.frames.forEach((f, i) => {
        const s = worldToScreen(f.pos.x, f.pos.z);
        const isSel = physicsSel && physicsSel.path === pi && physicsSel.index === i;
        ctx.beginPath();
        ctx.arc(s.x, s.y, isSel ? 5 : 2.2, 0, Math.PI * 2);
        ctx.fillStyle = isSel ? '#ff5ea8' : '#ff9c3c';
        ctx.fill();
        if (isSel) {
          ctx.lineWidth = 2; ctx.strokeStyle = '#ffffff'; ctx.stroke();
          ctx.fillStyle = '#ffd7ea'; ctx.font = '10px system-ui';
          ctx.fillText(`phys ${pi}.${i}`, s.x + 8, s.y - 6);
        }
      });
    });
  }

  // self-intersection markers: one dot per detected crossing, coloured by
  // effective state. Click to cycle auto -> keep -> collapse -> auto. Filled
  // disc = the loop is collapsed (removed); hollow ring = the loop is kept.
  // Red/green = user override; amber/grey = automatic. A dark halo keeps them
  // visible on any ribbon/centerline colour.
  for (const list of crossingCache) {
    for (const cr of list || []) {
      const state = crossingState(cr);
      const s = worldToScreen(cr.world.x, cr.world.z);
      const collapsed = state === 'auto-collapse' || state === 'forced-collapse';
      const col = CROSSING_COLORS[state];
      // dark contrast halo
      ctx.beginPath(); ctx.arc(s.x, s.y, 9, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(0,0,0,0.6)'; ctx.fill();
      // coloured disc (collapsed) or ring (kept)
      ctx.beginPath(); ctx.arc(s.x, s.y, 6.5, 0, Math.PI * 2);
      ctx.lineWidth = 2.5; ctx.strokeStyle = col;
      if (collapsed) { ctx.fillStyle = col; ctx.fill(); }
      ctx.stroke();
      // small centre pip so kept (hollow) markers still read as a target
      if (!collapsed) { ctx.beginPath(); ctx.arc(s.x, s.y, 1.6, 0, Math.PI * 2); ctx.fillStyle = col; ctx.fill(); }
    }
  }

  // start marker + direction arrow
  clampStart();
  const sf = startFrame();
  const p0 = worldToScreen(sf.pos.x, sf.pos.z);
  let dirX = sf.tangent.x, dirZ = sf.tangent.z;
  if (track.start.reverse) { dirX = -dirX; dirZ = -dirZ; }
  const ang = Math.atan2(dirX, dirZ); // screen-space heading (matches worldToScreen's x/z -> x/y mapping)
  const alen = 22;
  const ax = p0.x + Math.sin(ang) * alen, ay = p0.y + Math.cos(ang) * alen;
  ctx.strokeStyle = '#8dff9d'; ctx.lineWidth = 2.5;
  ctx.beginPath(); ctx.moveTo(p0.x, p0.y); ctx.lineTo(ax, ay); ctx.stroke();
  const headAng = Math.atan2(ax - p0.x, ay - p0.y);
  ctx.beginPath();
  ctx.moveTo(ax, ay);
  ctx.lineTo(ax - Math.sin(headAng - 0.4) * 8, ay - Math.cos(headAng - 0.4) * 8);
  ctx.lineTo(ax - Math.sin(headAng + 0.4) * 8, ay - Math.cos(headAng + 0.4) * 8);
  ctx.closePath(); ctx.fillStyle = '#8dff9d'; ctx.fill();
  ctx.fillStyle = '#8dff9d';
  ctx.font = '11px system-ui'; ctx.fillText('START', p0.x + 10, p0.y - 10);

  // selected segment highlights. Selecting a position point shows the previous
  // centerline segment in green and the next centerline segment in solid red.
  // Delete / the props button target the red segment by default.
  if (pointFilters.position) {
    const drawSegmentHighlight = (highlight, color) => {
      if (!highlight) return;
      const cps = pathPreviews[highlight.path].parts.controlPoints;
      const a = cps[highlight.i], b = cps[(highlight.i + 1) % cps.length];
      const sa = worldToScreen(a.pos[0], a.pos[2]), sb = worldToScreen(b.pos[0], b.pos[2]);
      ctx.strokeStyle = color; ctx.lineWidth = 4;
      ctx.setLineDash([]);
      ctx.beginPath(); ctx.moveTo(sa.x, sa.y); ctx.lineTo(sb.x, sb.y); ctx.stroke();
    };
    drawSegmentHighlight(selectedIncomingSegment(), '#31d66b');
    drawSegmentHighlight(segSel || selectedOutgoingSegment(), '#ff3344');
  }

  // control-point nodes (square = open-path endpoint, eligible for Join).
  // Shared disjoint points may appear more than once as path endpoints; draw
  // each logical point once so the UI matches the shared-object model.
  const drawnPositionIds = new Set();
  if (pointFilters.position) track.paths.forEach((path, pi) => {
    const cps = pathPreviews[pi].parts.controlPoints;
    cps.forEach((p, i) => {
      if (p.id && drawnPositionIds.has(p.id)) return;
      if (p.id) drawnPositionIds.add(p.id);
      const s = worldToScreen(p.pos[0], p.pos[2]);
      const isSel = p === curPoint() && !rollSel && !widthSel;
      const isEndpoint = !path.closed && (i === 0 || i === cps.length - 1);
      const isJoinDragSource = !!joinDragFrom && joinDragFrom.path === pi && joinDragFrom.point === i;
      const isJoinDragTarget = !!joinDragTarget && joinDragTarget.path === pi && joinDragTarget.point === i;
      const inJoinSel = joinSel.some(j => j.path === pi && j.point === i) || isJoinDragSource;
      const r = isSel ? 8 : 6;
      ctx.beginPath();
      if (isEndpoint) { ctx.rect(s.x - r, s.y - r, r * 2, r * 2); }
      else { ctx.arc(s.x, s.y, r, 0, Math.PI * 2); }
      ctx.fillStyle = isJoinDragTarget ? '#31d66b' : (inJoinSel ? '#ffd23c' : heightColor(p.pos[1]));
      ctx.fill();
      const isDisjoint = !!seamForPoint(p);
      ctx.lineWidth = isSel || isDisjoint ? 3 : 1.5;
      ctx.strokeStyle = isSel ? '#ffffff' : (isDisjoint ? '#ffcc44' : 'rgba(0,0,0,0.6)');
      ctx.stroke();
      if (isDisjoint) {
        ctx.strokeStyle = '#ffcc44'; ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(s.x - r - 4, s.y - r - 4); ctx.lineTo(s.x + r + 4, s.y + r + 4);
        ctx.moveTo(s.x + r + 4, s.y - r - 4); ctx.lineTo(s.x - r - 4, s.y + r + 4);
        ctx.stroke();
      }
      ctx.fillStyle = '#bfe6f7'; ctx.font = '10px system-ui';
      ctx.fillText(`${pi}.${i} (y${p.pos[1].toFixed(0)})`, s.x + 9, s.y + 3);
    });
  });

  // Shift-drag-to-connect: live line from the dragged endpoint to the cursor
  // (green + snapped to the node when hovering a valid drop point, yellow +
  // following the cursor otherwise).
  if (joinDragFrom && joinDragScreen) {
    const fromP = pathPreviews[joinDragFrom.path].parts.controlPoints[joinDragFrom.point];
    const from = worldToScreen(fromP.pos[0], fromP.pos[2]);
    let to = joinDragScreen;
    if (joinDragTarget) {
      const toP = pathPreviews[joinDragTarget.path].parts.controlPoints[joinDragTarget.point];
      to = worldToScreen(toP.pos[0], toP.pos[2]);
    }
    ctx.strokeStyle = joinDragTarget ? '#31d66b' : '#ffd23c';
    ctx.lineWidth = 2; ctx.setLineDash([6, 4]);
    ctx.beginPath(); ctx.moveTo(from.x, from.y); ctx.lineTo(to.x, to.y); ctx.stroke();
    ctx.setLineDash([]);
  }

  // Draft curve being created.
  if (createDraft.length) {
    ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 2; ctx.setLineDash([4, 4]);
    ctx.beginPath();
    createDraft.forEach((p, i) => {
      const s = worldToScreen(p.pos[0], p.pos[2]);
      i ? ctx.lineTo(s.x, s.y) : ctx.moveTo(s.x, s.y);
    });
    ctx.stroke(); ctx.setLineDash([]);
    createDraft.forEach((p, i) => {
      const s = worldToScreen(p.pos[0], p.pos[2]);
      ctx.beginPath(); ctx.arc(s.x, s.y, i === 0 ? 7 : 5, 0, Math.PI * 2);
      ctx.fillStyle = i === 0 ? '#8dff9d' : '#ffffff'; ctx.fill();
      ctx.strokeStyle = '#000'; ctx.stroke();
    });
  }

  // roll control points of the currently-selected path (created in the
  // elevation strip or here) -- shown here too, at their position along the
  // curve, so they're visible/selectable from the top-down view as well.
  const curPrev = pathPreviews[sel.path];
  const curP = track.paths[sel.path];
  if (curPrev && curP) {
    const frames = curPrev.frames;
    if (pointFilters.roll) curPrev.parts.rollPoints.forEach(rp => {
      const f = frameAtT(frames, curP.closed, rp.t);
      const s = worldToScreen(f.pos.x, f.pos.z);
      const isSel = rp === rollSel;
      const isDraggingThis = isSel && dragging === 'rollTop';

      // Perpendicular indicator line: right (along +h) if roll > 0, left if
      // roll < 0, length scaled by how much of the full +-180 range is used.
      const end = rollLineEnd(f, rp.roll);
      const e = worldToScreen(end.x, end.z);
      ctx.strokeStyle = rollTint(rp.roll); ctx.lineWidth = isSel ? 3 : 2;
      ctx.beginPath(); ctx.moveTo(s.x, s.y); ctx.lineTo(e.x, e.y); ctx.stroke();

      // draggable handle at the line's end -- drag to change roll
      const hr = isDraggingThis ? 7 : 5;
      ctx.beginPath(); ctx.arc(e.x, e.y, hr, 0, Math.PI * 2);
      ctx.fillStyle = '#ffffff';
      ctx.fill();
      ctx.lineWidth = isDraggingThis ? 3 : 1.5;
      ctx.strokeStyle = rollTint(rp.roll);
      ctx.stroke();
    });

    // width control points of the currently-selected path -- a line spanning
    // the full track width at that point (both edges, symmetric about the
    // centerline) -- just the line and its two edge handles, no center dot.
    if (pointFilters.width) curPrev.parts.widthPoints.forEach(wp => {
      const f = frameAtT(frames, curP.closed, wp.t);
      const isSel = wp === widthSel;
      const isDraggingThis = isSel && dragging === 'widthTop';
      const halfW = Math.max(1, wp.width) / 2;
      const rightW = { x: f.pos.x + f.h.x * halfW, z: f.pos.z + f.h.z * halfW };
      const leftW = { x: f.pos.x - f.h.x * halfW, z: f.pos.z - f.h.z * halfW };
      const rightS = worldToScreen(rightW.x, rightW.z);
      const leftS = worldToScreen(leftW.x, leftW.z);

      ctx.strokeStyle = WIDTH_COLOR; ctx.lineWidth = isSel ? 3 : 2;
      ctx.beginPath(); ctx.moveTo(leftS.x, leftS.y); ctx.lineTo(rightS.x, rightS.y); ctx.stroke();

      // draggable handles at both edges -- drag either to change width
      const hr = isDraggingThis ? 7 : 5;
      for (const hs of [leftS, rightS]) {
        ctx.beginPath(); ctx.arc(hs.x, hs.y, hr, 0, Math.PI * 2);
        ctx.fillStyle = '#ffffff';
        ctx.fill();
        ctx.lineWidth = isDraggingThis ? 3 : 1.5;
        ctx.strokeStyle = WIDTH_COLOR;
        ctx.stroke();
      }
    });

    // cross-section curvature control points -- a purple handle offset from
    // the centerline along +h/-h. Right = positive dome, left = inverted.
    if (pointFilters.crossSection) curPrev.parts.crossSectionPoints.forEach(cp => {
      const f = frameAtT(frames, curP.closed, cp.t);
      const s = worldToScreen(f.pos.x, f.pos.z);
      const len = (f.width / 2) * Math.max(-1, Math.min(1, cp.curvature));
      const h = worldToScreen(f.pos.x + f.h.x * len, f.pos.z + f.h.z * len);
      const isSel = cp === crossSectionSel;
      ctx.strokeStyle = CROSS_SECTION_COLOR; ctx.lineWidth = isSel ? 3 : 2;
      ctx.beginPath(); ctx.moveTo(s.x, s.y); ctx.lineTo(h.x, h.y); ctx.stroke();
      ctx.beginPath(); ctx.rect(h.x - (isSel ? 7 : 5), h.y - (isSel ? 7 : 5), (isSel ? 14 : 10), (isSel ? 14 : 10));
      ctx.fillStyle = '#ffffff'; ctx.fill();
      ctx.lineWidth = isSel ? 3 : 1.5; ctx.strokeStyle = CROSS_SECTION_COLOR; ctx.stroke();
    });
  }
}

// Baked frame at a path's fractional parameter t (same t domain the roll
// spline uses: 0 = start, 1 = end/wrap-back-to-start).
function frameAtT(frames, closed, t) {
  const N = frames.length;
  const idx = closed ? Math.round(t * N) % N : Math.max(0, Math.min(N - 1, Math.round(t * (N - 1))));
  return frames[idx];
}

// World-space endpoint of a roll point's perpendicular indicator line, given
// its baked frame and roll value (right/+h for roll>0, left/-h for roll<0,
// length scaled by how much of the full +-180 range is used).
function rollLineEnd(f, rollDeg) {
  const sign = rollDeg > 0 ? 1 : rollDeg < 0 ? -1 : 0;
  const len = f.width * (Math.min(180, Math.abs(rollDeg)) / 180);
  return { x: f.pos.x + f.h.x * sign * len, z: f.pos.z + f.h.z * sign * len };
}

// ---------- Draw the elevation strip ----------
// Two INDEPENDENT sets of draggable handles share this strip:
//   - height handles (circles): one per position control point, unchanged.
//   - roll handles (diamonds): the path's roll-type points, a separate set of
//     control points (own count, own spacing) driving the roll spline.
let elevGeom = { padX: 30, top: 20, bottom: 20, yScale: 1, yMid: 0 };

/* Round-number spacing for an axis: the smallest 1/2/5 x 10^n step that keeps
 * the tick count near `targetTicks`. Picking the step from the data range
 * instead of a fixed increment is what keeps the labels readable at every zoom
 * -- a flat track spanning 3 units and a mountain spanning 300 both get about
 * the same number of ticks, at values a person would actually choose. */
function niceAxisStep(range, targetTicks) {
  const raw = range / Math.max(1, targetTicks);
  if (!(raw > 0) || !isFinite(raw)) return 1;
  const magnitude = Math.pow(10, Math.floor(Math.log10(raw)));
  const normalized = raw / magnitude;
  const step = normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 5 ? 5 : 10;
  return step * magnitude;
}

/* Y-axis scale for the elevation strip: a faint full-width gridline at each
 * round elevation, labelled in the left gutter. Full-width rather than short
 * ticks because the point is reading a HEIGHT off the profile at some point
 * along the track, which means carrying the eye across the panel.
 *
 * Drawn before everything else so it sits behind the profile, the handles and
 * the mesh line. The tick that lands on zero is skipped: the zero line is drawn
 * straight after this with its own emphasis and its own 'y=0' label, and two
 * labels in the same few pixels just collide. */
function drawElevYAxis(ctx, geom) {
  const { padX, top, bottom, minY, maxY, yScale, w } = geom;
  const range = maxY - minY;
  if (!(range > 0) || !isFinite(range)) return;
  // Roughly one label per 34px of panel height, so a taller panel gets a finer
  // scale rather than the same few lines stretched further apart.
  const step = niceAxisStep(range, Math.max(2, Math.round((bottom - top) / 34)));
  const decimals = Math.max(0, Math.min(3, -Math.floor(Math.log10(step) + 1e-9)));

  ctx.save();
  ctx.font = '10px system-ui';
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  ctx.lineWidth = 1;
  for (let v = Math.ceil(minY / step) * step; v <= maxY + step * 1e-6; v += step) {
    const y = bottom - (v - minY) * yScale;
    if (y < top - 0.5 || y > bottom + 0.5) continue;
    const isZero = Math.abs(v) < step * 1e-6;
    if (isZero) continue;                       // the zero line labels itself
    ctx.strokeStyle = 'rgba(60,95,125,0.34)';
    ctx.beginPath(); ctx.moveTo(padX, y); ctx.lineTo(w - padX, y); ctx.stroke();
    ctx.fillStyle = '#5c7f95';
    // `+v.toFixed()` strips a "-0" from values that round to zero.
    ctx.fillText(String(+v.toFixed(decimals)), padX - 6, y);
  }
  ctx.restore();
}
function drawElev() {
  // Collapsed: the canvas is display:none, so its rect is 0x0. Fitting and
  // drawing against that would overwrite elevGeom with nonsense (bottom = -20,
  // a negative yScale) that the hit-testers would read on the way back out.
  // Leaving the last good geometry in place costs nothing -- nothing can be
  // clicked in a hidden canvas, and expanding redraws before anything is.
  if (elevCollapsed) return;
  const { w, h } = fitCanvas(elevCanvas, elevCtx);
  const ctx = elevCtx;
  ctx.clearRect(0, 0, w, h);
  const path = curPath();
  if (!path) { elevGeom = { padX: 30, top: 20, bottom: h - 20, minY: -1, maxY: 1, yScale: 1, w, h, closed: true, n: 0 }; return; }
  const pp = curParts();
  const cps = pp.controlPoints;
  const n = cps.length;
  const closed = path.closed;
  const slots = closed ? n + 1 : n; // closed: extra slot on the right echoes point 0

  let minY = Infinity, maxY = -Infinity;
  for (const p of cps) { minY = Math.min(minY, p.pos[1]); maxY = Math.max(maxY, p.pos[1]); }
  // Keep the selected mesh's height inside the plotted range, so its line stays
  // on-panel even when it sits well above or below this curve -- the whole
  // point of showing it here is judging it against the curve's profile.
  const selectedMeshPlacement = selectedPlacement();
  if (selectedMeshPlacement) {
    minY = Math.min(minY, selectedMeshPlacement.elevation);
    maxY = Math.max(maxY, selectedMeshPlacement.elevation);
  }
  const pad = Math.max(8, (maxY - minY) * 0.3 + 4);
  minY -= pad; maxY += pad;
  // padX also reserves the gutter the Y-axis labels are right-aligned into, so
  // it has to fit the widest tick text (see drawElevYAxis).
  const top = 20, bottom = h - 20, padX = 44;
  const yScale = (bottom - top) / ((maxY - minY) || 1);

  // Sample the ACTUAL interpolated spline (same evaluator the game/top-down
  // view use), sub-dividing each control-point span, and use cumulative XZ
  // (top-down) arc length along that sampled curve as the x-axis metric. Slot
  // x-positions (for height handles) are just this arc length evaluated
  // exactly at each control point, so handles stay aligned with the curve
  // under them. `profile` samples are also indexed by the SAME fractional
  // parameter t = g/gMax that roll points use, so roll-handle positions can
  // be looked up directly by index.
  const { evalTrack } = TrackCore.makeEvaluator(cps, closed, pp.rollPoints, pp.widthPoints, pp.crossSectionPoints);
  const SUB = 16;
  const segCount = slots - 1;
  const profile = [];   // { arc, y, roll(deg) } samples along the curve, evenly spaced in t
  const cpArc = [0];    // arc length at each control-point slot
  let prevPos = evalTrack(0).pos, cum = 0;
  {
    const f0 = evalTrack(0);
    profile.push({ arc: 0, y: f0.pos.y, roll: f0.roll * 180 / Math.PI });
  }
  for (let k = 0; k < segCount; k++) {
    for (let s = 1; s <= SUB; s++) {
      const f = evalTrack(k + s / SUB);
      cum += Math.hypot(f.pos.x - prevPos.x, f.pos.z - prevPos.z);
      prevPos = f.pos;
      profile.push({ arc: cum, y: f.pos.y, roll: f.roll * 180 / Math.PI });
    }
    cpArc.push(cum);
  }
  const totalArc = cum || 1;
  const totalSamples = profile.length - 1; // profile[i] is at t = i / totalSamples
  const xs = cpArc.map(d => padX + (d / totalArc) * (w - 2 * padX));
  const px = arc => padX + (arc / totalArc) * (w - 2 * padX);
  const sampleAtT = t => profile[Math.max(0, Math.min(totalSamples, Math.round(Math.max(0, Math.min(1, t)) * totalSamples)))];

  // Roll's pixels-per-degree ratio is scaled to the panel's own height so the
  // full +-180 degree range is always visible (rather than clipping/clamping
  // at a fixed ratio): half the available plot height maps to 180 degrees.
  const rollK = Math.max(0.02, (bottom - top - 2 * ROLL_HANDLE_MARGIN) / 2 / 180);

  elevGeom = { padX, top, bottom, minY, maxY, yScale, w, h, closed, n, xs, profile, totalArc, totalSamples, rollK };

  drawElevYAxis(ctx, elevGeom);

  // zero line
  const zeroY = bottom - (0 - minY) * yScale;
  ctx.strokeStyle = 'rgba(70,110,140,0.5)'; ctx.setLineDash([4, 4]); ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(padX, zeroY); ctx.lineTo(w - padX, zeroY); ctx.stroke(); ctx.setLineDash([]);
  // Labelled like every other tick -- right-aligned in the same gutter -- so the
  // scale reads as one clean column of numbers instead of one stray left-aligned
  // string. Kept brighter than the rest to hold the zero line's emphasis.
  ctx.save();
  ctx.font = '10px system-ui'; ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
  ctx.fillStyle = '#8fb4c8';
  ctx.fillText('0', padX - 6, zeroY);
  ctx.restore();

  // Selected mesh region: one draggable horizontal line at its elevation. A
  // mesh has no path parameter, so it spans the full width rather than being
  // positioned along the x-axis; drag it to line the region up with the part
  // of this curve's profile you want it to meet.
  if (selectedMeshPlacement) {
    const my = bottom - (selectedMeshPlacement.elevation - minY) * yScale;
    ctx.strokeStyle = dragging === 'meshElev' ? '#f0e4ff' : '#b98cff';
    ctx.lineWidth = dragging === 'meshElev' ? 3 : 2;
    ctx.beginPath(); ctx.moveTo(padX, my); ctx.lineTo(w - padX, my); ctx.stroke();
    ctx.fillStyle = '#d8bcff'; ctx.font = '10px system-ui';
    ctx.fillText(`${selectedMeshPlacement.asset}  y ${selectedMeshPlacement.elevation.toFixed(1)}`, padX + 4, my - 4);
  }

  const hy = y => bottom - (y - minY) * yScale;
  // Roll offsets (rollY = track height +/- roll*rollK) are unrelated to the
  // Y-elevation autofit; rollK above is scaled so +-180 fits, and this clamp
  // is just a safety net for off-center track-height lines.
  const clampY = y => Math.max(top + ROLL_HANDLE_MARGIN, Math.min(bottom - ROLL_HANDLE_MARGIN, y));

  // interpolated height profile (the actual baked spline, not straight lines
  // between control points)
  ctx.strokeStyle = 'rgba(79,214,255,0.8)'; ctx.lineWidth = 2;
  ctx.beginPath();
  profile.forEach((p, i) => {
    const x = px(p.arc), y = hy(p.y); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
  });
  ctx.stroke();

  // Roll is intentionally not drawn in the side view; edit roll control points
  // from the top-down view so this panel stays focused on elevation.

  // height handles (circles; closed: the last one is a faded echo of point 0)
  const drawnElevIds = new Set();
  if (pointFilters.position) for (let i = 0; i < slots; i++) {
    const idx = i % n;
    const p = cps[idx];
    if (!closed && p.id && drawnElevIds.has(p.id)) continue;
    const echo = closed && i === n;
    if (!echo && p.id) drawnElevIds.add(p.id);
    const x = xs[i], y = hy(p.pos[1]);
    const isSel = idx === sel.point && !rollSel && !widthSel;
    ctx.globalAlpha = echo ? 0.45 : 1;
    ctx.beginPath(); ctx.arc(x, y, isSel ? 7 : 5, 0, Math.PI * 2);
    ctx.fillStyle = heightColor(p.pos[1]); ctx.fill();
    const isDisjoint = !!seamForPoint(p);
    ctx.lineWidth = isSel || isDisjoint ? 3 : 1.5;
    ctx.strokeStyle = isSel ? '#fff' : (isDisjoint ? '#ffcc44' : 'rgba(0,0,0,0.6)'); ctx.stroke();
    ctx.globalAlpha = 1;
    ctx.fillStyle = '#7fb8d8'; ctx.font = '9px system-ui';
    ctx.fillText(echo ? '0↺' : String(i), x - 4, bottom + 14);
  }
}

function draw() { drawTop(); drawElev(); }

// ---------- Convert a control point's type ----------
// Removes the currently-selected point (whichever type it is) and re-adds it
// as `newType`, computing its initial value by interpolating the *remaining*
// control points of the target type on either side of its position (t) along
// the path -- i.e. the same spline the target type already uses, just
// evaluated once. Converting TO position looks up the position spline's XYZ;
// converting FROM position uses its logical index (t = index / pointCount) as
// the interpolation point on the roll/width spline it's converted to.
function convertSelectedPoint(newType) {
  const path = curPath();
  if (!path) return;
  const pp = parts(path);

  let curType, curObj, t;
  if (crossSectionSel) { curType = 'crossSection'; curObj = crossSectionSel; t = crossSectionSel.t; }
  else if (widthSel) { curType = 'width'; curObj = widthSel; t = widthSel.t; }
  else if (rollSel) { curType = 'roll'; curObj = rollSel; t = rollSel.t; }
  else {
    const p = curPoint();
    if (!p) return;
    curType = 'position'; curObj = p;
    const N = pp.controlPoints.length;
    const idx = pp.controlPoints.indexOf(p);
    t = path.closed ? idx / N : idx / Math.max(1, N - 1);
  }
  if (curType === newType) return;

  if (curType === 'position' && countPointOccurrences(curObj) > 1) {
    alert('Reconnect this shared/disjoint point before converting it.'); return;
  }
  if (curType === 'position' && pp.controlPoints.length <= 4) {
    alert('A track path needs at least 4 position control points.'); return;
  }
  if (curType === 'roll' && pp.rollPoints.length <= 2) {
    alert('A path needs at least 2 roll points.'); return;
  }
  if (curType === 'width' && pp.widthPoints.length <= 2) {
    alert('A path needs at least 2 width points.'); return;
  }
  if (curType === 'crossSection' && pp.crossSectionPoints.length <= 2) {
    alert('A path needs at least 2 cross-section points.'); return;
  }

  pushUndo();
  path.points.splice(path.points.indexOf(curObj), 1);
  const remaining = parts(path); // curObj now excluded -- these are the "control points on either side"

  let created;
  if (newType === 'position') {
    const { evalTrack, CP_N } = TrackCore.makeEvaluator(remaining.controlPoints, path.closed, remaining.rollPoints, remaining.widthPoints, remaining.crossSectionPoints);
    const gMax = path.closed ? CP_N : CP_N - 1;
    const s = evalTrack(t * gMax);
    created = { type: 'position', id: newId('p'), pos: [+s.pos.x.toFixed(1), +s.pos.y.toFixed(1), +s.pos.z.toFixed(1)], weight: 1 };
    const insertIdx = Math.round(t * (path.closed ? remaining.controlPoints.length : Math.max(1, remaining.controlPoints.length - 1)));
    insertPositionAt(path, Math.max(0, Math.min(remaining.controlPoints.length, insertIdx)), created);
  } else if (newType === 'roll') {
    const rollDeg = TrackCore.evalRoll(remaining.rollPoints, path.closed, t) * 180 / Math.PI;
    created = { type: 'roll', t, roll: Math.round(rollDeg * 10) / 10 };
    path.points.push(created);
  } else if (newType === 'width') {
    const width = TrackCore.evalWidth(remaining.widthPoints, path.closed, t);
    created = { type: 'width', t, width: Math.round(width * 10) / 10 };
    path.points.push(created);
  } else { // crossSection
    const curvature = TrackCore.evalCrossSectionCurvature(remaining.crossSectionPoints, path.closed, t);
    const tightness = TrackCore.evalCrossSectionTightness(remaining.crossSectionPoints, path.closed, t);
    const thickness = TrackCore.evalCrossSectionThickness(remaining.crossSectionPoints, path.closed, t);
    created = crossSectionPoint(t,
      Math.round(curvature * 100) / 100,
      Math.round(tightness * 10) / 10,
      Math.round(thickness * 10) / 10);
    path.points.push(created);
  }

  rollSel = null; widthSel = null; crossSectionSel = null;
  if (newType === 'roll') rollSel = created;
  else if (newType === 'width') widthSel = created;
  else if (newType === 'crossSection') crossSectionSel = created;
  else selectPosition(sel.path, positionIndices(path).indexOf(path.points.indexOf(created)));
  assertNoStaleSeams();
  refresh();
}

// ---------- Disjoint corner topology ----------
function seamForPoint(point) {
  return point && track.disjointSeams && track.disjointSeams.find(s => s.pointId === point.id);
}
function pathHasDisjointSeam(path) {
  if (!path || !track.disjointSeams) return false;
  return track.disjointSeams.some(s =>
    s.pathId === path.id || s.leftPathId === path.id || s.rightPathId === path.id
  );
}
function seamIsValid(seam) {
  if (!seam) return false;
  if (!track.paths.some(path => parts(path).controlPoints.some(p => p.id === seam.pointId))) return false;
  if (seam.kind === 'opened-closed') {
    const path = track.paths.find(p => p.id === seam.pathId);
    if (!path) return false;
    const pos = parts(path).controlPoints;
    return pos.length >= 2 && pos[0].id === seam.pointId && pos[pos.length - 1].id === seam.pointId;
  }
  if (seam.kind === 'split-open') {
    const leftPath = track.paths.find(p => p.id === seam.leftPathId);
    const rightPath = track.paths.find(p => p.id === seam.rightPathId);
    if (!leftPath || !rightPath) return false;
    const left = parts(leftPath).controlPoints, right = parts(rightPath).controlPoints;
    return !!left.length && !!right.length && left[left.length - 1].id === seam.pointId && right[0].id === seam.pointId;
  }
  return false;
}
function junctionIsValid(j) {
  if (!j) return false;
  const exists = track.paths.some(path => parts(path).controlPoints.some(p => p.id === j.pointId));
  return exists;
}
function allPositionIds() {
  const ids = new Set();
  for (const path of track.paths) for (const p of parts(path).controlPoints) if (p.id) ids.add(p.id);
  return ids;
}
// A self-intersection override is stale once either endpoint control point it
// was anchored to no longer exists.
function overrideIsValid(o, ids) {
  return !!o && ids.has(o.a) && ids.has(o.b);
}
function removeStaleSeams() {
  const before = (track.disjointSeams || []).length;
  track.disjointSeams = (track.disjointSeams || []).filter(seamIsValid);
  const beforeJ = (track.junctions || []).length;
  track.junctions = (track.junctions || []).filter(junctionIsValid);
  const beforeO = (track.selfIntersectionOverrides || []).length;
  const ids = allPositionIds();
  track.selfIntersectionOverrides = (track.selfIntersectionOverrides || []).filter(o => overrideIsValid(o, ids));
  return (before - track.disjointSeams.length) + (beforeJ - track.junctions.length)
    + (beforeO - track.selfIntersectionOverrides.length);
}
function assertNoStaleSeams() {
  const removed = removeStaleSeams();
  if (removed) console.warn(`Removed ${removed} stale disjoint/junction record(s).`);
}
function countPointOccurrences(point) {
  if (!point) return 0;
  let count = 0;
  for (const path of track.paths) {
    for (const p of parts(path).controlPoints) if (p === point || p.id === point.id) count++;
  }
  return count;
}
function findPointOccurrence(point) {
  if (!point) return null;
  for (let pi = 0; pi < track.paths.length; pi++) {
    const cps = parts(track.paths[pi]).controlPoints;
    const idx = cps.findIndex(p => p === point || p.id === point.id);
    if (idx >= 0) return { path: pi, point: idx };
  }
  return null;
}
function startPointObject() {
  if (!track.start || !track.paths[track.start.path]) return null;
  return parts(track.paths[track.start.path]).controlPoints[track.start.point] || null;
}
function preserveStartPoint(startPoint, reverse) {
  const occ = findPointOccurrence(startPoint);
  if (occ) track.start = { path: occ.path, point: occ.point, reverse: !!reverse };
  else clampStart();
}
const round1 = n => Math.round(n * 10) / 10;
const roundT = n => Math.round(n * 1000) / 1000;
const rollDegAt = (partsObj, closed, t) => round1(TrackCore.evalRoll(partsObj.rollPoints, closed, t) * 180 / Math.PI);
const widthAt = (partsObj, closed, t) => round1(TrackCore.evalWidth(partsObj.widthPoints, closed, t));
function averageRollDeg(a, b) {
  const ar = a * Math.PI / 180, br = b * Math.PI / 180;
  return round1(Math.atan2(Math.sin(ar) + Math.sin(br), Math.cos(ar) + Math.cos(br)) * 180 / Math.PI);
}
/* Collapse points that land on the same `t` -- the last one wins, whole.
 *
 * These arrays are built by pushing synthesized range endpoints first and then
 * the authored interior points, whose remapped t is rounded to 3dp; an interior
 * point near a range end can round straight onto the endpoint's t. Array order
 * is therefore meaningful, and JS sort is stable, so "last" is always the
 * authored point rather than the synthesized sample -- which is the one worth
 * keeping.
 *
 * It replaces the whole point deliberately. This used to take a value key and
 * copy just that one field onto the survivor, which worked while every point
 * type had exactly one value -- then cross-section points gained `tightness`
 * alongside `curvature` and the call site still passed only 'curvature'. The
 * survivor came out with one point's curvature and the other's tightness: a
 * pair nobody authored. Swapping the object cannot develop that bug again, no
 * matter what fields a point type grows later.
 *
 * Every element here is a freshly built literal (never an object aliased from
 * path.points), so replacing rather than mutating is safe for callers. */
function uniqueScalarPoints(points) {
  points.sort((a, b) => a.t - b.t);
  const out = [];
  for (const p of points) {
    const prev = out[out.length - 1];
    if (prev && Math.abs(prev.t - p.t) < 1e-5) out[out.length - 1] = p;
    else out.push(p);
  }
  return out;
}
// A cross-section point at `t`, with all three values sampled off an existing
// path's cross-section spline at `sourceT`. Used wherever a split, join or seam
// reconnect has to synthesize a range endpoint that matches the source curve.
const crossSectionSampleAt = (xs, closed, sourceT, t) => crossSectionPoint(t,
  round1(TrackCore.evalCrossSectionCurvature(xs, closed, sourceT)),
  round1(TrackCore.evalCrossSectionTightness(xs, closed, sourceT)),
  round1(TrackCore.evalCrossSectionThickness(xs, closed, sourceT)));

function rollWidthForSourceRange(sourceParts, sourceClosed, startT, endT) {
  // Preserve authored roll/width points that lie inside the source range,
  // remapping their t values, and synthesize exact range endpoints. For closed
  // paths `endT` may be > 1 to represent a wrapped one-cycle interval.
  const span = endT - startT || 1;
  const inRange = t => t > startT + 1e-5 && t < endT - 1e-5;
  const mapT = t => Math.max(0, Math.min(1, (t - startT) / span));
  const sourceT = t => sourceClosed ? ((t % 1) + 1) % 1 : Math.max(0, Math.min(1, t));
  const rollPoints = [
    { type: 'roll', t: 0, roll: rollDegAt(sourceParts, sourceClosed, sourceT(startT)) },
    { type: 'roll', t: 1, roll: rollDegAt(sourceParts, sourceClosed, sourceT(endT)) }
  ];
  const widthPoints = [
    { type: 'width', t: 0, width: widthAt(sourceParts, sourceClosed, sourceT(startT)) },
    { type: 'width', t: 1, width: widthAt(sourceParts, sourceClosed, sourceT(endT)) }
  ];
  const crossSectionPoints = [
    crossSectionSampleAt(sourceParts.crossSectionPoints, sourceClosed, sourceT(startT), 0),
    crossSectionSampleAt(sourceParts.crossSectionPoints, sourceClosed, sourceT(endT), 1)
  ];
  for (const rp of sourceParts.rollPoints) {
    let t = rp.t;
    if (sourceClosed && endT > 1 && t < startT) t += 1;
    if (inRange(t)) rollPoints.push({ type: 'roll', t: roundT(mapT(t)), roll: rp.roll });
  }
  for (const wp of sourceParts.widthPoints) {
    let t = wp.t;
    if (sourceClosed && endT > 1 && t < startT) t += 1;
    if (inRange(t)) widthPoints.push({ type: 'width', t: roundT(mapT(t)), width: wp.width });
  }
  for (const cp of sourceParts.crossSectionPoints) {
    let t = cp.t;
    if (sourceClosed && endT > 1 && t < startT) t += 1;
    if (inRange(t)) crossSectionPoints.push(crossSectionCopy(roundT(mapT(t)), cp));
  }
  return uniqueScalarPoints(rollPoints).concat(uniqueScalarPoints(widthPoints), uniqueScalarPoints(crossSectionPoints));
}
function sampleRollWidthFromPath(path, startT, endT) {
  return rollWidthForSourceRange(parts(path), path.closed, startT, endT);
}
function sampleRollWidthForClosedReconnect(openPath) {
  const pp = parts(openPath);
  const seamRoll = averageRollDeg(rollDegAt(pp, false, 0), rollDegAt(pp, false, 1));
  const seamWidth = round1((widthAt(pp, false, 0) + widthAt(pp, false, 1)) / 2);
  const seamCurvature = round1((TrackCore.evalCrossSectionCurvature(pp.crossSectionPoints, false, 0) + TrackCore.evalCrossSectionCurvature(pp.crossSectionPoints, false, 1)) / 2);
  const seamTightness = round1((TrackCore.evalCrossSectionTightness(pp.crossSectionPoints, false, 0) + TrackCore.evalCrossSectionTightness(pp.crossSectionPoints, false, 1)) / 2);
  const seamThickness = round1((TrackCore.evalCrossSectionThickness(pp.crossSectionPoints, false, 0) + TrackCore.evalCrossSectionThickness(pp.crossSectionPoints, false, 1)) / 2);
  const rollPoints = [
    { type: 'roll', t: 0, roll: seamRoll },
    { type: 'roll', t: 1, roll: seamRoll }
  ];
  const widthPoints = [
    { type: 'width', t: 0, width: seamWidth },
    { type: 'width', t: 1, width: seamWidth }
  ];
  const crossSectionPoints = [
    crossSectionPoint(0, seamCurvature, seamTightness, seamThickness),
    crossSectionPoint(1, seamCurvature, seamTightness, seamThickness)
  ];
  for (const rp of pp.rollPoints) if (rp.t > 1e-5 && rp.t < 1 - 1e-5) rollPoints.push({ type: 'roll', t: rp.t, roll: rp.roll });
  for (const wp of pp.widthPoints) if (wp.t > 1e-5 && wp.t < 1 - 1e-5) widthPoints.push({ type: 'width', t: wp.t, width: wp.width });
  for (const cp of pp.crossSectionPoints) if (cp.t > 1e-5 && cp.t < 1 - 1e-5) crossSectionPoints.push(crossSectionCopy(cp.t, cp));
  return uniqueScalarPoints(rollPoints).concat(uniqueScalarPoints(widthPoints), uniqueScalarPoints(crossSectionPoints));
}
function sampleRollWidthFromJoinedPaths(leftPath, rightPath, seamIndex, mergedCount) {
  const leftParts = parts(leftPath), rightParts = parts(rightPath);
  const maxG = Math.max(1, mergedCount - 1);
  const rightSpan = Math.max(1, mergedCount - 1 - seamIndex);
  const seamT = roundT(seamIndex / maxG);
  const leftRollAtSeam = rollDegAt(leftParts, false, 1);
  const rightRollAtSeam = rollDegAt(rightParts, false, 0);
  const leftWidthAtSeam = widthAt(leftParts, false, 1);
  const rightWidthAtSeam = widthAt(rightParts, false, 0);
  const leftCurvatureAtSeam = TrackCore.evalCrossSectionCurvature(leftParts.crossSectionPoints, false, 1);
  const rightCurvatureAtSeam = TrackCore.evalCrossSectionCurvature(rightParts.crossSectionPoints, false, 0);
  const leftTightnessAtSeam = TrackCore.evalCrossSectionTightness(leftParts.crossSectionPoints, false, 1);
  const rightTightnessAtSeam = TrackCore.evalCrossSectionTightness(rightParts.crossSectionPoints, false, 0);
  const leftThicknessAtSeam = TrackCore.evalCrossSectionThickness(leftParts.crossSectionPoints, false, 1);
  const rightThicknessAtSeam = TrackCore.evalCrossSectionThickness(rightParts.crossSectionPoints, false, 0);
  const rollPoints = [
    { type: 'roll', t: 0, roll: rollDegAt(leftParts, false, 0) },
    { type: 'roll', t: 1, roll: rollDegAt(rightParts, false, 1) }
  ];
  const widthPoints = [
    { type: 'width', t: 0, width: widthAt(leftParts, false, 0) },
    { type: 'width', t: 1, width: widthAt(rightParts, false, 1) }
  ];
  const crossSectionPoints = [
    crossSectionSampleAt(leftParts.crossSectionPoints, false, 0, 0),
    crossSectionSampleAt(rightParts.crossSectionPoints, false, 1, 1)
  ];
  if (seamT > 1e-5 && seamT < 1 - 1e-5) {
    // Preserve the seam as an explicit scalar control. If the two sides were
    // edited differently while split, reconcile them with a local average so
    // reconnecting doesn't let Catmull-Rom smear a discontinuity far away.
    rollPoints.push({ type: 'roll', t: seamT, roll: averageRollDeg(leftRollAtSeam, rightRollAtSeam) });
    widthPoints.push({ type: 'width', t: seamT, width: round1((leftWidthAtSeam + rightWidthAtSeam) / 2) });
    crossSectionPoints.push(crossSectionPoint(seamT, round1((leftCurvatureAtSeam + rightCurvatureAtSeam) / 2), round1((leftTightnessAtSeam + rightTightnessAtSeam) / 2), round1((leftThicknessAtSeam + rightThicknessAtSeam) / 2)));
  }
  for (const rp of leftParts.rollPoints) if (rp.t > 1e-5 && rp.t < 1 - 1e-5) rollPoints.push({ type: 'roll', t: roundT((rp.t * seamIndex) / maxG), roll: rp.roll });
  for (const rp of rightParts.rollPoints) if (rp.t > 1e-5 && rp.t < 1 - 1e-5) rollPoints.push({ type: 'roll', t: roundT((seamIndex + rp.t * rightSpan) / maxG), roll: rp.roll });
  for (const wp of leftParts.widthPoints) if (wp.t > 1e-5 && wp.t < 1 - 1e-5) widthPoints.push({ type: 'width', t: roundT((wp.t * seamIndex) / maxG), width: wp.width });
  for (const wp of rightParts.widthPoints) if (wp.t > 1e-5 && wp.t < 1 - 1e-5) widthPoints.push({ type: 'width', t: roundT((seamIndex + wp.t * rightSpan) / maxG), width: wp.width });
  for (const cp of leftParts.crossSectionPoints) if (cp.t > 1e-5 && cp.t < 1 - 1e-5) crossSectionPoints.push(crossSectionCopy(roundT((cp.t * seamIndex) / maxG), cp));
  for (const cp of rightParts.crossSectionPoints) if (cp.t > 1e-5 && cp.t < 1 - 1e-5) crossSectionPoints.push(crossSectionCopy(roundT((seamIndex + cp.t * rightSpan) / maxG), cp));
  return uniqueScalarPoints(rollPoints).concat(uniqueScalarPoints(widthPoints), uniqueScalarPoints(crossSectionPoints));
}
function disjointDisabledReason(path, pointIndex) {
  const cps = parts(path).controlPoints;
  if (!path.closed && (pointIndex === 0 || pointIndex === cps.length - 1)) return 'Open endpoints are already disjoint.';
  if (!path.closed) {
    const leftCount = pointIndex + 1;
    const rightCount = cps.length - pointIndex;
    if (leftCount < 4 || rightCount < 4) return 'Need at least 4 position points on both sides.';
  }
  return '';
}
function updateDisjointSeamsForPathMerge(removedPathIds, mergedPath, ignoredSeam) {
  const mergedCps = parts(mergedPath).controlPoints;
  const startsAt = pointId => mergedCps.length && mergedCps[0].id === pointId;
  const endsAt = pointId => mergedCps.length && mergedCps[mergedCps.length - 1].id === pointId;
  for (const seam of track.disjointSeams || []) {
    if (seam === ignoredSeam) continue;
    if (seam.kind === 'split-open') {
      const touchesLeft = removedPathIds.includes(seam.leftPathId);
      const touchesRight = removedPathIds.includes(seam.rightPathId);
      if (touchesLeft && touchesRight && startsAt(seam.pointId) && endsAt(seam.pointId)) {
        seam.kind = 'opened-closed';
        seam.pathId = mergedPath.id;
        delete seam.leftPathId;
        delete seam.rightPathId;
      } else {
        if (touchesLeft && endsAt(seam.pointId)) seam.leftPathId = mergedPath.id;
        if (touchesRight && startsAt(seam.pointId)) seam.rightPathId = mergedPath.id;
      }
    } else if (seam.kind === 'opened-closed' && removedPathIds.includes(seam.pathId) && startsAt(seam.pointId) && endsAt(seam.pointId)) {
      seam.pathId = mergedPath.id;
    }
  }
}

function updateDisjointSeamsForPathSplit(oldPathId, leftPath, rightPath) {
  const leftCps = parts(leftPath).controlPoints, rightCps = parts(rightPath).controlPoints;
  const replacementEndingAt = pointId => {
    if (leftCps.length && leftCps[leftCps.length - 1].id === pointId) return leftPath.id;
    if (rightCps.length && rightCps[rightCps.length - 1].id === pointId) return rightPath.id;
    return null;
  };
  const replacementStartingAt = pointId => {
    if (leftCps.length && leftCps[0].id === pointId) return leftPath.id;
    if (rightCps.length && rightCps[0].id === pointId) return rightPath.id;
    return null;
  };
  for (const seam of track.disjointSeams || []) {
    if (seam.kind === 'opened-closed' && seam.pathId === oldPathId) {
      const ending = replacementEndingAt(seam.pointId);
      const starting = replacementStartingAt(seam.pointId);
      if (ending && starting) {
        seam.kind = 'split-open';
        delete seam.pathId;
        seam.leftPathId = ending;
        seam.rightPathId = starting;
      }
    } else if (seam.kind === 'split-open') {
      if (seam.leftPathId === oldPathId) {
        const ending = replacementEndingAt(seam.pointId);
        if (ending) seam.leftPathId = ending;
      }
      if (seam.rightPathId === oldPathId) {
        const starting = replacementStartingAt(seam.pointId);
        if (starting) seam.rightPathId = starting;
      }
    }
  }
}

function makeDisjoint() {
  const path = curPath(), pp = curParts(), point = curPoint();
  if (!path || !point || seamForPoint(point)) return;
  const oldStartPoint = startPointObject(), oldReverse = !!(track.start && track.start.reverse);
  const reason = disjointDisabledReason(path, sel.point);
  if (reason) { alert(reason); return; }
  pushUndo();
  const posObjs = pp.controlPoints;
  const seam = { id: newId('seam'), pointId: point.id };
  if (path.closed) {
    const splitIndex = sel.point;
    const originalCount = posObjs.length;
    const rotated = posObjs.slice(splitIndex).concat(posObjs.slice(0, splitIndex), point);
    const startT = splitIndex / originalCount;
    const rollWidth = rollWidthForSourceRange(pp, true, startT, startT + 1);
    path.points = rotated.concat(rollWidth);
    path.closed = false;
    seam.kind = 'opened-closed';
    seam.pathId = path.id;
    selectedPointId = point.id;
    syncSelectionToId();
  } else {
    const splitIndex = sel.point;
    const originalMax = Math.max(1, posObjs.length - 1);
    const left = posObjs.slice(0, splitIndex + 1);
    const right = posObjs.slice(splitIndex);
    const leftPath = {
      id: newId('path'), closed: false,
      points: left.concat(rollWidthForSourceRange(pp, false, 0, splitIndex / originalMax))
    };
    const rightPath = {
      id: newId('path'), closed: false,
      points: right.concat(rollWidthForSourceRange(pp, false, splitIndex / originalMax, 1))
    };
    updateDisjointSeamsForPathSplit(path.id, leftPath, rightPath);
    track.paths.splice(sel.path, 1, leftPath, rightPath);
    seam.kind = 'split-open';
    seam.leftPathId = leftPath.id;
    seam.rightPathId = rightPath.id;
    selectedPointId = point.id;
    syncSelectionToId();
  }
  track.disjointSeams.push(seam);
  preserveStartPoint(oldStartPoint, oldReverse);
  assertNoStaleSeams();
  segSel = null; joinSel = []; rollSel = null; widthSel = null; crossSectionSel = null;
  refresh();
}
function reconnectDisjoint() {
  const point = curPoint();
  const seam = seamForPoint(point);
  if (!point || !seam) return;
  if (!seamIsValid(seam)) { alert('Cannot reconnect: this disjoint seam is stale.'); return; }
  const oldStartPoint = startPointObject(), oldReverse = !!(track.start && track.start.reverse);
  if (seam.kind === 'opened-closed') {
    const pi = track.paths.findIndex(p => p.id === seam.pathId);
    if (pi < 0) { alert('Cannot reconnect: original path is gone.'); return; }
    const path = track.paths[pi];
    const pos = parts(path).controlPoints;
    if (pos.length < 2 || pos[0].id !== seam.pointId || pos[pos.length - 1].id !== seam.pointId) {
      alert('Cannot reconnect: seam endpoints no longer match.'); return;
    }
    pushUndo();
    const closedPositions = pos.slice(0, -1);
    path.points = closedPositions.concat(sampleRollWidthForClosedReconnect(path));
    path.closed = true;
    selectedPointId = point.id;
    syncSelectionToId();
  } else if (seam.kind === 'split-open') {
    const li = track.paths.findIndex(p => p.id === seam.leftPathId);
    const ri = track.paths.findIndex(p => p.id === seam.rightPathId);
    if (li < 0 || ri < 0) { alert('Cannot reconnect: split paths are gone.'); return; }
    const leftPath = track.paths[li], rightPath = track.paths[ri];
    const left = parts(leftPath).controlPoints;
    const right = parts(rightPath).controlPoints;
    if (!left.length || !right.length || left[left.length - 1].id !== seam.pointId || right[0].id !== seam.pointId) {
      alert('Cannot reconnect: seam endpoints no longer match.'); return;
    }
    pushUndo();
    const merged = left.concat(right.slice(1));
    const mergedPath = {
      id: leftPath.id, closed: false,
      points: merged.concat(sampleRollWidthFromJoinedPaths(leftPath, rightPath, left.length - 1, merged.length))
    };
    updateDisjointSeamsForPathMerge([leftPath.id, rightPath.id], mergedPath, seam);
    const lo = Math.min(li, ri), hi = Math.max(li, ri);
    track.paths.splice(hi, 1);
    track.paths.splice(lo, 1, mergedPath);
    selectedPointId = point.id;
    syncSelectionToId();
  }
  track.disjointSeams.splice(track.disjointSeams.indexOf(seam), 1);
  preserveStartPoint(oldStartPoint, oldReverse);
  assertNoStaleSeams();
  segSel = null; joinSel = []; rollSel = null; widthSel = null; crossSectionSel = null;
  refresh();
}

// ---------- Properties panel ----------
function drawCrossSectionPreview(point) {
  const canvas = document.getElementById('crossSectionPreview');
  if (!canvas || !point) return;
  const ctx = canvas.getContext('2d');
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth || 200, h = canvas.clientHeight || 120;
  canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);

  const path = curPath();
  if (!path) return;
  const pp = parts(path);
  const width = TrackCore.evalWidth(pp.widthPoints, path.closed, point.t);
  const curvature = Math.max(-1, Math.min(1, point.curvature || 0));
  const tightness = Math.max(0.2, Math.min(4, point.tightness == null ? 1 : point.tightness));
  const thickness = Math.max(0, point.thickness == null ? TrackCore.DEFAULT_CROSS_SECTION_THICKNESS : point.thickness);
  const pad = 18;
  const STEPS = 48;
  // Same profile the game's ribbon and the USD exporter use, so the preview
  // shows the surface that will actually be built.
  const heightAt = v => TrackCore.crossSectionHeight(curvature, tightness, v, width);

  // Fit the whole extruded section, not just the road surface: the slab hangs
  // `thickness` below the profile and would otherwise run off the canvas. The
  // vertical span is measured rather than assumed, since a dished cross-section
  // already reaches below the chord before any extrusion.
  let hiY = 0, loY = 0;
  for (let i = 0; i <= STEPS; i++) {
    const y = heightAt(i / STEPS);
    hiY = Math.max(hiY, y); loY = Math.min(loY, y);
  }
  loY -= thickness;
  const midX = w / 2, midY = h / 2;
  const scale = Math.min((w - 2 * pad) / Math.max(width, 1), (h - 2 * pad) / Math.max(hiY - loY, 1));
  const centreY = (hiY + loY) / 2;
  const px = x => midX + x * scale;
  const py = y => midY - (y - centreY) * scale;
  const vx = v => px(-width / 2 + width * v);
  const traceSurface = (offset = 0) => {
    for (let i = 0; i <= STEPS; i++) {
      const v = i / STEPS, x = vx(v), y = py(heightAt(v) + offset);
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    }
  };

  // flat chord reference
  const chordY = py(0);
  ctx.strokeStyle = 'rgba(111,147,168,0.45)'; ctx.lineWidth = 1; ctx.setLineDash([4, 3]);
  ctx.beginPath(); ctx.moveTo(pad, chordY); ctx.lineTo(w - pad, chordY); ctx.stroke(); ctx.setLineDash([]);
  ctx.fillStyle = '#6f93a8'; ctx.font = '10px system-ui';
  ctx.fillText('flat', pad, chordY - 4);

  if (thickness > 0) {
    // The extruded slab: road surface on top, the same profile offset below it.
    ctx.beginPath();
    traceSurface(0);
    for (let i = STEPS; i >= 0; i--) {
      const v = i / STEPS;
      ctx.lineTo(vx(v), py(heightAt(v) - thickness));
    }
    ctx.closePath();
    ctx.fillStyle = 'rgba(120,152,184,0.28)';
    ctx.fill();
    ctx.strokeStyle = 'rgba(164,192,220,0.55)'; ctx.lineWidth = 1; ctx.stroke();
  } else {
    // Zero thickness is still a legal sheet; show curvature as the area between
    // the flat chord and the road, which is what this preview always showed.
    ctx.beginPath();
    ctx.moveTo(vx(0), chordY);
    traceSurface(0);
    ctx.lineTo(vx(1), chordY);
    ctx.closePath();
    ctx.fillStyle = curvature >= 0 ? 'rgba(213,140,255,0.25)' : 'rgba(255,140,213,0.25)';
    ctx.fill();
  }

  // road surface curve, drawn last so it reads as the driving surface
  ctx.strokeStyle = CROSS_SECTION_COLOR; ctx.lineWidth = 2.5;
  ctx.beginPath(); traceSurface(0); ctx.stroke();

  // edge markers and center marker
  for (const v of [0, 0.5, 1]) {
    const x = vx(v), y = py(heightAt(v));
    ctx.beginPath(); ctx.arc(x, y, v === 0.5 ? 4 : 3, 0, Math.PI * 2);
    ctx.fillStyle = v === 0.5 ? '#fff' : '#d58cff'; ctx.fill();
  }

  ctx.fillStyle = '#cdeeff'; ctx.font = '11px system-ui';
  ctx.fillText(`curvature ${curvature.toFixed(2)} · tightness ${tightness.toFixed(2)}`, pad, h - 8);
  ctx.textAlign = 'right';
  ctx.fillText(`width ${width.toFixed(1)} · thick ${thickness.toFixed(1)}`, w - pad, h - 8);
  ctx.textAlign = 'left';
}

function renderProps() {
  const body = document.getElementById('propBody');
  // Typing in a number field fires 'input' per keystroke; collapse a whole
  // typing session into one undo step by only pushing on the first keystroke
  // since this panel was (re)rendered -- a fresh render (new point selected,
  // or the point deselected) naturally starts a new session.
  let historyArmed = false;
  const armHistory = () => { if (!historyArmed) { pushUndo(); historyArmed = true; updateUndoRedoButtons(); } };
  const typeSelectRow = (current) =>
    `<label>Type<select id="typeSelect">` +
    `<option value="position"${current === 'position' ? ' selected' : ''}>Position</option>` +
    `<option value="roll"${current === 'roll' ? ' selected' : ''}>Roll</option>` +
    `<option value="width"${current === 'width' ? ' selected' : ''}>Width</option>` +
    `<option value="crossSection"${current === 'crossSection' ? ' selected' : ''}>Cross-section</option>` +
    `</select></label>`;
  const wireTypeSelect = () => {
    document.getElementById('typeSelect').addEventListener('change', (e) => convertSelectedPoint(e.target.value));
  };

  // Physics sample point (debug overlay). Read-only: these frames are baked
  // from the authored curve by buildCenterline, not stored, so there is nothing
  // to edit here -- the panel just exposes the exact values physics consumes.
  if (physicsSel) {
    const prev = pathPreviews[physicsSel.path];
    const frame = prev && prev.frames[physicsSel.index];
    if (!prev || !frame) { physicsSel = null; }
    else {
      const path = track.paths[physicsSel.path];
      const closed = path ? path.closed !== false : true;
      const N = prev.frames.length;
      const t = closed ? physicsSel.index / N : (N > 1 ? physicsSel.index / (N - 1) : 0);
      const v3 = v => `${v.x.toFixed(3)}, ${v.y.toFixed(3)}, ${v.z.toFixed(3)}`;
      const rowRO = (label, val) =>
        `<label style="cursor:default"><span>${label}</span><span style="color:#ffb877;font-variant-numeric:tabular-nums">${val}</span></label>`;
      const left = prev.edges.left[physicsSel.index], right = prev.edges.right[physicsSel.index];
      body.innerHTML =
        `<div style="margin-bottom:6px;color:#ff9c3c">Physics sample <b>${physicsSel.path}.${physicsSel.index}</b></div>` +
        `<div style="margin-bottom:8px;color:#6f93a8;font-size:11px">baked frame ${physicsSel.index + 1} of ${N} &middot; ${closed ? 'closed' : 'open'} path</div>` +
        rowRO('t (param)', t.toFixed(4)) +
        rowRO('Position', v3(frame.pos)) +
        rowRO('Tangent', v3(frame.tangent)) +
        rowRO('Roll&deg;', (frame.roll * 180 / Math.PI).toFixed(2)) +
        rowRO('Width', frame.width.toFixed(3)) +
        rowRO('Half width', frame.halfW.toFixed(3)) +
        rowRO('Edge right', v3(frame.edgeRight)) +
        rowRO('Normal', v3(frame.normal)) +
        (left ? rowRO('Left edge', v3(left)) : '') +
        (right ? rowRO('Right edge', v3(right)) : '') +
        `<div class="hint">Read-only. N=${N} uniform reference frames of the curve physics/collision rides on (buildCenterline); in-game the sample count scales with track length. Left/right edges include self-intersection trimming, matching the game corridor.</div>`;
      return;
    }
  }

  // Mesh regions own the panel whenever one is selected. Rail height is a
  // property of the ASSET, so editing it here changes every placement of that
  // shape -- matching where the rail flags themselves live.
  const meshPlacement = selectedPlacement();
  if (meshPlacement) {
    const asset = (track.meshAssets || {})[meshPlacement.asset];
    const mesh = assetMesh(meshPlacement.asset);
    const railCount = mesh ? [...mesh.edges.values()].filter(edge => edge.attributes?.rail).length : 0;
    const edgeCount = mesh ? mesh.edges.size : 0;
    const placedTwice = meshPlacements().filter(m => m.asset === meshPlacement.asset).length;
    body.innerHTML =
      `<div style="margin-bottom:6px;color:#b98cff">Mesh region <b>${meshPlacement.id}</b></div>` +
      `<div style="margin-bottom:8px;color:#6f93a8;font-size:11px">asset <b>${meshPlacement.asset}</b>` +
      (placedTwice > 1 ? ` &middot; placed ${placedTwice}&times;` : '') + `</div>` +
      `<label>X<input type="number" data-mesh="x" value="${meshPlacement.x}" step="0.5"></label>` +
      `<label>Z<input type="number" data-mesh="z" value="${meshPlacement.z}" step="0.5"></label>` +
      `<label>Elevation<input type="number" data-mesh="elevation" value="${meshPlacement.elevation}" step="0.5"></label>` +
      `<label>Rotation&deg;<input type="number" data-mesh="rotation" value="${meshPlacement.rotation}" step="5"></label>` +
      `<label title="Applies to every placement of this asset">Rail height<input type="number" data-asset="railHeight" value="${asset ? asset.railHeight : TrackCore.DEFAULT_RAIL_HEIGHT}" step="0.5" min="0"></label>` +
      `<div class="hint">${railCount} of ${edgeCount} edges railed. Switch to <b>Rails</b> mode to click edges: railed edges are solid walls, bare edges are ledges you drive off.</div>` +
      `<button id="delMeshBtn" style="margin-top:10px;width:100%;background:#3d2a5a;border:1px solid #b98cff;color:#efe2ff;border-radius:5px;padding:6px;cursor:pointer">Delete mesh region</button>`;
    body.querySelectorAll('input').forEach(inp => {
      inp.addEventListener('input', () => {
        const val = parseFloat(inp.value);
        if (!isFinite(val)) return;
        armHistory();
        if (inp.dataset.mesh) meshPlacement[inp.dataset.mesh] = val;
        else if (inp.dataset.asset && asset) asset.railHeight = Math.max(0, val);
        // draw(), NOT refresh() -- refresh() calls renderProps(), which rebuilds
        // this panel via innerHTML and so destroys the very input being typed
        // into: the field lost focus after a single character, and the fresh
        // render reset armHistory's flag, so every keystroke also pushed its own
        // undo entry and 30 characters evicted the whole history. Every sibling
        // handler below (roll, width, cross-section, position) already calls
        // draw() for exactly this reason. persistEditorTrack() is kept so the
        // game's live preview still follows along, which is the only other part
        // of refresh() a mesh edit affects -- updateMeta() and
        // renderTexturePanel() read nothing that a placement or rail height can
        // change.
        draw();
        persistEditorTrack();
      });
    });
    document.getElementById('delMeshBtn').addEventListener('click', deleteSelectedMesh);
    return;
  }

  if (crossSectionSel) {
    const path = curPath();
    const pp = path ? parts(path) : null;
    const idx = pp ? pp.crossSectionPoints.indexOf(crossSectionSel) : -1;
    if (path && idx >= 0) {
      body.innerHTML =
        `<div style="margin-bottom:6px;color:#7fb8d8">Path ${sel.path}, cross-section point #${idx}</div>` +
        typeSelectRow('crossSection') +
        `<label>Position (%)<input type="number" data-key="t" value="${(crossSectionSel.t * 100).toFixed(1)}" step="1" min="0" max="100"></label>` +
        `<label>Curvature<input type="number" data-key="curvature" value="${crossSectionSel.curvature}" step="0.05" min="-1" max="1"></label>` +
        `<label title="Higher values pinch the curve tighter around the center; 1 is circular.">Tightness<input type="number" data-key="tightness" value="${crossSectionSel.tightness == null ? 1 : crossSectionSel.tightness}" step="0.1" min="0.2" max="4"></label>` +
        `<label title="How far the road is extruded downward into a solid shell. 0 leaves it an infinitely thin sheet.">Thickness<input type="number" data-key="thickness" value="${crossSectionSel.thickness == null ? TrackCore.DEFAULT_CROSS_SECTION_THICKNESS : crossSectionSel.thickness}" step="0.5" min="0"></label>` +
        `<div style="margin-top:10px;color:#7fb8d8;font-size:11px;text-transform:uppercase;letter-spacing:.06em">Cross-section preview</div>` +
        `<canvas id="crossSectionPreview" style="display:block;width:100%;height:170px;margin-top:5px;background:#071019;border:1px solid #244358;border-radius:5px"></canvas>` +
        `<button id="delCrossSectionBtn" ${pp.crossSectionPoints.length <= 2 ? 'disabled' : ''} style="margin-top:10px;width:100%;background:#4a235a;border:1px solid #d58cff;color:#f4ddff;border-radius:5px;padding:6px;cursor:pointer">Delete cross-section point (min 2)</button>`;
      wireTypeSelect();
      drawCrossSectionPreview(crossSectionSel);
      body.querySelectorAll('input').forEach(inp => {
        inp.addEventListener('input', () => {
          const v = parseFloat(inp.value);
          if (!isFinite(v)) return;
          armHistory();
          if (inp.dataset.key === 't') crossSectionSel.t = Math.max(0, Math.min(1, v / 100));
          else if (inp.dataset.key === 'tightness') crossSectionSel.tightness = Math.max(0.2, Math.min(4, v));
          else if (inp.dataset.key === 'thickness') crossSectionSel.thickness = Math.max(0, v);
          else crossSectionSel.curvature = Math.max(-1, Math.min(1, v));
          draw();
          drawCrossSectionPreview(crossSectionSel);
        });
      });
      document.getElementById('delCrossSectionBtn').addEventListener('click', () => {
        if (pp.crossSectionPoints.length <= 2) { alert('A path needs at least 2 cross-section points.'); return; }
        pushUndo();
        path.points.splice(path.points.indexOf(crossSectionSel), 1);
        crossSectionSel = null;
        refresh();
      });
      return;
    }
  }

  if (widthSel) {
    const path = curPath();
    const pp = path ? parts(path) : null;
    const idx = pp ? pp.widthPoints.indexOf(widthSel) : -1;
    if (path && idx >= 0) {
      body.innerHTML =
        `<div style="margin-bottom:6px;color:#7fb8d8">Path ${sel.path}, width point #${idx}</div>` +
        typeSelectRow('width') +
        `<label>Position (%)<input type="number" data-key="t" value="${(widthSel.t * 100).toFixed(1)}" step="1" min="0" max="100"></label>` +
        `<label>Width<input type="number" data-key="width" value="${widthSel.width}" step="1" min="1"></label>` +
        `<button id="delWidthBtn" ${pp.widthPoints.length <= 2 ? 'disabled' : ''} style="margin-top:10px;width:100%;background:#5a1f2a;border:1px solid #a34;color:#fbd;border-radius:5px;padding:6px;cursor:pointer">Delete width point (min 2)</button>`;
      wireTypeSelect();
      body.querySelectorAll('input').forEach(inp => {
        inp.addEventListener('input', () => {
          const v = parseFloat(inp.value);
          if (!isFinite(v)) return;
          armHistory();
          if (inp.dataset.key === 't') widthSel.t = Math.max(0, Math.min(1, v / 100));
          else widthSel.width = Math.max(1, v);
          draw();
        });
      });
      document.getElementById('delWidthBtn').addEventListener('click', () => {
        if (pp.widthPoints.length <= 2) { alert('A path needs at least 2 width points.'); return; }
        pushUndo();
        path.points.splice(path.points.indexOf(widthSel), 1);
        widthSel = null;
        refresh();
      });
      return;
    }
  }

  if (rollSel) {
    const path = curPath();
    const pp = path ? parts(path) : null;
    const idx = pp ? pp.rollPoints.indexOf(rollSel) : -1;
    if (path && idx >= 0) {
      body.innerHTML =
        `<div style="margin-bottom:6px;color:#7fb8d8">Path ${sel.path}, roll point #${idx}</div>` +
        typeSelectRow('roll') +
        `<label>Position (%)<input type="number" data-key="t" value="${(rollSel.t * 100).toFixed(1)}" step="1" min="0" max="100"></label>` +
        `<label>Roll&deg;<input type="number" data-key="roll" value="${rollSel.roll}" step="1" min="-180" max="180"></label>` +
        `<button id="delRollBtn" ${pp.rollPoints.length <= 2 ? 'disabled' : ''} style="margin-top:10px;width:100%;background:#5a1f2a;border:1px solid #a34;color:#fbd;border-radius:5px;padding:6px;cursor:pointer">Delete roll point (min 2)</button>`;
      wireTypeSelect();
      body.querySelectorAll('input').forEach(inp => {
        inp.addEventListener('input', () => {
          const v = parseFloat(inp.value);
          if (!isFinite(v)) return;
          armHistory();
          if (inp.dataset.key === 't') rollSel.t = Math.max(0, Math.min(1, v / 100));
          else rollSel.roll = Math.max(-180, Math.min(180, v));
          draw();
        });
      });
      document.getElementById('delRollBtn').addEventListener('click', () => {
        if (pp.rollPoints.length <= 2) { alert('A path needs at least 2 roll points.'); return; }
        pushUndo();
        path.points.splice(path.points.indexOf(rollSel), 1);
        rollSel = null;
        refresh();
      });
      return;
    }
  }

  const p = curPoint();
  if (!p) { body.innerHTML = '<div class="none">No point selected.</div>'; return; }
  const path = curPath();
  clampStart();
  const isStart = track.start.path === sel.path && track.start.point === sel.point;
  const seam = seamForPoint(p);
  const disjointReason = seam ? '' : disjointDisabledReason(path, sel.point);
  const row = (key, label, val, stepv) =>
    `<label>${label}<input type="number" data-key="${key}" value="${val}" step="${stepv}"></label>`;
  const disjointHelp = seam
    ? 'Hard corner is active. Uncheck to reconnect this editor-created seam.'
    : (disjointReason || 'Split/open this path here to create a hard shared corner.');
  const incomingSeg = selectedIncomingSegment();
  const outgoingSeg = selectedOutgoingSegment();
  const pointDeleteDisabled = countPointOccurrences(p) > 1 || curParts().controlPoints.length <= 4;
  const greenBtn = incomingSeg
    ? `<button id="delPrevSegmentBtn" style="margin-top:6px;width:100%;background:#1f4f2d;border:1px solid #3b7;color:#caffd8;border-radius:5px;padding:6px;cursor:pointer">Delete green segment (previous)</button>`
    : '';
  const redBtn = outgoingSeg
    ? `<button id="delSegmentBtn" style="margin-top:6px;width:100%;background:#5a2f1f;border:1px solid #c64;color:#ffd7bd;border-radius:5px;padding:6px;cursor:pointer">Delete red segment (next)</button>`
    : '';
  body.innerHTML =
    `<div style="margin-bottom:6px;color:#7fb8d8">Path ${sel.path} (${path.closed ? 'closed' : 'open'}), point #${sel.point}</div>` +
    typeSelectRow('position') +
    row('x', 'X', p.pos[0], 1) +
    row('z', 'Z', p.pos[2], 1) +
    row('y', 'Y (elev)', p.pos[1], 0.5) +
    row('weight', 'Weight', p.weight, 0.1) +
    `<label title="${disjointHelp}">Disjoint corner<input id="disjointChk" type="checkbox" ${seam ? 'checked' : ''} ${disjointReason ? 'disabled' : ''}></label>` +
    `<div style="color:${disjointReason ? '#ff99aa' : '#6f93a8'};font-size:11px;line-height:1.35;margin:-2px 0 8px">${disjointHelp}</div>` +
    `<button id="startBtn" ${isStart ? 'disabled' : ''} style="margin-top:10px;width:100%;background:#123a26;border:1px solid #2c9e5a;color:#bdf7d4;border-radius:5px;padding:6px;cursor:pointer">${isStart ? 'This is the start point' : 'Set as start point'}</button>` +
    `<button id="delBtn" ${pointDeleteDisabled ? 'disabled' : ''} style="margin-top:6px;width:100%;background:#5a1f2a;border:1px solid #a34;color:#fbd;border-radius:5px;padding:6px;cursor:pointer">Delete point (min 4)</button>` +
    greenBtn + redBtn;
  wireTypeSelect();
  if (countPointOccurrences(p) > 1) document.getElementById('typeSelect').disabled = true;

  body.querySelectorAll('input').forEach(inp => {
    inp.addEventListener('input', () => {
      const v = parseFloat(inp.value);
      if (!isFinite(v)) return;
      armHistory();
      const key = inp.dataset.key;
      if (key === 'x') p.pos[0] = v;
      else if (key === 'y') p.pos[1] = v;
      else if (key === 'z') p.pos[2] = v;
      else p.weight = Math.max(0.01, v);
      draw();
    });
  });
  document.getElementById('delBtn').addEventListener('click', deleteSelected);
  if (outgoingSeg) document.getElementById('delSegmentBtn').addEventListener('click', () => deleteSelectedSegment('outgoing'));
  if (incomingSeg) document.getElementById('delPrevSegmentBtn').addEventListener('click', () => deleteSelectedSegment('incoming'));
  document.getElementById('disjointChk').addEventListener('change', (e) => {
    if (e.target.checked) makeDisjoint(); else reconnectDisjoint();
  });
  document.getElementById('startBtn').addEventListener('click', () => {
    pushUndo();
    track.start = { path: sel.path, point: sel.point, reverse: !!(track.start && track.start.reverse) };
    refresh();
  });
}

function updateCurveSelect() {
  const selEl = document.getElementById('curveSelect');
  if (!selEl) return;
  const old = String(sel.path);
  selEl.innerHTML = track.paths.map((p, i) => `<option value="${i}"${i === sel.path ? ' selected' : ''}>Curve ${i}${p.closed ? ' (closed)' : ''}</option>`).join('');
  if (selEl.value !== old && track.paths[sel.path]) selEl.value = String(sel.path);
}
function updateMeta() {
  syncSelectionToId();
  updateCurveSelect();
  updateUndoRedoButtons();
  const uniquePositions = new Set();
  track.paths.forEach(p => parts(p).controlPoints.forEach(cp => uniquePositions.add(cp.id || cp)));
  const total = uniquePositions.size;
  const seamCount = (track.disjointSeams || []).length;
  const junctionCount = (track.junctions || []).length;
  const staleIds = allPositionIds();
  const staleSeams = (track.disjointSeams || []).filter(s => !seamIsValid(s)).length + (track.junctions || []).filter(j => !junctionIsValid(j)).length
    + (track.selfIntersectionOverrides || []).filter(o => !overrideIsValid(o, staleIds)).length;
  document.getElementById('metaHint').innerHTML =
    `${track.paths.length} path(s), ${total} unique position control points total` +
    (seamCount ? `, ${seamCount} disjoint corner(s)` : '') +
    (junctionCount ? `, ${junctionCount} junction(s)` : '') +
    (staleSeams ? ` <span style="color:#ff6677">(${staleSeams} stale)</span>` : '') + `<br>` +
    (staleSeams ? `<button id="cleanStaleSeamsBtn" style="margin:4px 0 6px;width:100%;background:#4a2630;border:1px solid #a34;color:#fbd;border-radius:5px;padding:5px;cursor:pointer">Remove stale disjoint metadata</button>` : '') +
    `Cross-section points control local curvature (-100 = inverted semicircle, 0 = flat, 100 = semicircle).<br>` +
    `Roll tint: cyan = right-lean, magenta = left-lean.<br>` +
    `Node color = elevation. Square node = open-path endpoint.<br>` +
    `<span style="color:#b6ff3c">Width points</span> (top-down only) set the track's width independently of position/roll points.<br>` +
    `Change a selected point's "Type" in the panel above to convert it, interpolating from its neighbours.`;
  if (staleSeams) {
    document.getElementById('cleanStaleSeamsBtn').addEventListener('click', () => {
      pushUndo();
      removeStaleSeams();
      refresh();
    });
  }
  document.getElementById('nameInput').value = track.name || '';
  document.getElementById('joinBtn').disabled = joinSel.length !== 2;
  const deleteCurveBtn = document.getElementById('deleteCurveBtn');
  if (deleteCurveBtn) deleteCurveBtn.disabled = track.paths.length <= 1;
  clampStart();
  updateDirBtn();
}

function syncVisualOptionsFromControls() {
  const renderEl = document.getElementById('renderModeSelect');
  if (renderEl) renderMode = renderEl.value;
  const modeEl = document.getElementById('editModeSelect');
  if (modeEl) editMode = modeEl.value;
  const gridEl = document.getElementById('gridSizeSelect');
  if (gridEl) gridSize = Number(gridEl.value) || gridSize;
  const snapEl = document.getElementById('snapGridChk');
  if (snapEl) snapToGrid = snapEl.checked;
  const posEl = document.getElementById('showPositionChk');
  const rollEl = document.getElementById('showRollChk');
  const widthEl = document.getElementById('showWidthChk');
  const crossEl = document.getElementById('showCrossSectionChk');
  if (posEl && rollEl && widthEl && crossEl) {
    pointFilters = { position: posEl.checked, roll: rollEl.checked, width: widthEl.checked, crossSection: crossEl.checked };
    if (!pointFilters.roll) rollSel = null;
    if (!pointFilters.width) widthSel = null;
    if (!pointFilters.crossSection) crossSectionSel = null;
    if (!pointFilters.position) { segSel = null; joinSel = []; }
  }
}
function persistEditorTrack() {
  try { localStorage.setItem('web3d.currentTrack', TrackCore.serializeTrack(track)); }
  catch (err) { console.warn('Could not persist track for game preview:', err); }
}
function refresh() { syncVisualOptionsFromControls(); draw(); renderProps(); updateMeta(); renderTexturePanel(); persistEditorTrack(); }

// ---------- Hit testing ----------
// Roll-point markers of the currently-selected path, drawn in the top-down
// view alongside the position control-point nodes.
function rollNodeAtTop(sx, sy) {
  return null;
}
// The draggable handle at the end of a roll point's perpendicular line.
function rollHandleAtTop(sx, sy) {
  if (!pointFilters.roll) return null;
  const curPrev = pathPreviews[sel.path];
  const curP = track.paths[sel.path];
  if (!curPrev || !curP) return null;
  for (const rp of curPrev.parts.rollPoints) {
    const f = frameAtT(curPrev.frames, curP.closed, rp.t);
    const end = rollLineEnd(f, rp.roll);
    const s = worldToScreen(end.x, end.z);
    if (Math.hypot(sx - s.x, sy - s.y) <= 9) return rp;
  }
  return null;
}
// The draggable handles at either edge of a width point's line (no separate
// center marker -- the line + its two edge handles ARE the control point).
function widthHandleAtTop(sx, sy) {
  if (!pointFilters.width) return null;
  const curPrev = pathPreviews[sel.path];
  const curP = track.paths[sel.path];
  if (!curPrev || !curP) return null;
  for (const wp of curPrev.parts.widthPoints) {
    const f = frameAtT(curPrev.frames, curP.closed, wp.t);
    const halfW = Math.max(1, wp.width) / 2;
    const right = worldToScreen(f.pos.x + f.h.x * halfW, f.pos.z + f.h.z * halfW);
    const left = worldToScreen(f.pos.x - f.h.x * halfW, f.pos.z - f.h.z * halfW);
    if (Math.hypot(sx - right.x, sy - right.y) <= 9) return wp;
    if (Math.hypot(sx - left.x, sy - left.y) <= 9) return wp;
  }
  return null;
}
function crossSectionHandleAtTop(sx, sy) {
  if (!pointFilters.crossSection) return null;
  const curPrev = pathPreviews[sel.path];
  const curP = track.paths[sel.path];
  if (!curPrev || !curP) return null;
  for (const cp of curPrev.parts.crossSectionPoints) {
    const f = frameAtT(curPrev.frames, curP.closed, cp.t);
    const len = (f.width / 2) * Math.max(-1, Math.min(1, cp.curvature));
    const h = worldToScreen(f.pos.x + f.h.x * len, f.pos.z + f.h.z * len);
    if (Math.hypot(sx - h.x, sy - h.y) <= 10) return cp;
  }
  return null;
}
function nodeAtTop(sx, sy) {
  if (!pointFilters.position) return null;
  const seen = new Set();
  for (let pi = 0; pi < track.paths.length; pi++) {
    const cps = pathPreviews[pi].parts.controlPoints;
    for (let i = 0; i < cps.length; i++) {
      const p = cps[i];
      if (p.id && seen.has(p.id)) continue;
      if (p.id) seen.add(p.id);
      const s = worldToScreen(p.pos[0], p.pos[2]);
      if (Math.hypot(sx - s.x, sy - s.y) <= 10) return { path: pi, point: i };
    }
  }
  return null;
}
// Nearest baked physics frame (across all paths) to the cursor, when the debug
// overlay is on. Iterates the same frames drawTop renders; small threshold
// because the dots are dense.
function physicsPointAtTop(sx, sy) {
  if (!showPhysicsPoints) return null;
  let best = null, bestD = 8; // px
  pathPreviews.forEach((prev, pi) => {
    prev.frames.forEach((f, i) => {
      const s = worldToScreen(f.pos.x, f.pos.z);
      const d = Math.hypot(sx - s.x, sy - s.y);
      if (d < bestD) { bestD = d; best = { path: pi, index: i }; }
    });
  });
  return best;
}
// Nearest segment (control point i -> i+1, wrapping only if closed) across all
// paths, for right-click segment selection (deletion).
function segmentAtTop(sx, sy) {
  let best = null, bestD = 18; // px threshold
  track.paths.forEach((path, pi) => {
    const cps = pathPreviews[pi].parts.controlPoints;
    const segCount = path.closed ? cps.length : cps.length - 1;
    for (let i = 0; i < segCount; i++) {
      const a = worldToScreen(cps[i].pos[0], cps[i].pos[2]);
      const b = worldToScreen(cps[(i + 1) % cps.length].pos[0], cps[(i + 1) % cps.length].pos[2]);
      const dx = b.x - a.x, dy = b.y - a.y;
      const len2 = dx * dx + dy * dy;
      const t = len2 > 0 ? Math.max(0, Math.min(1, ((sx - a.x) * dx + (sy - a.y) * dy) / len2)) : 0;
      const px = a.x + dx * t, py = a.y + dy * t;
      const d = Math.hypot(sx - px, sy - py);
      if (d < bestD) { bestD = d; best = { path: pi, i }; }
    }
  });
  return best;
}
function handleAtElev(sx, sy) {
  if (!pointFilters.position) return -1;
  const path = curPath();
  if (!path) return -1;
  const cps = curParts().controlPoints;
  const n = elevGeom.n, closed = elevGeom.closed;
  const slots = closed ? n + 1 : n;
  const seen = new Set();
  for (let i = 0; i < slots; i++) {
    const idx = i % n;
    const p = cps[idx];
    if (p.id && seen.has(p.id)) continue;
    if (p.id) seen.add(p.id);
    const x = elevGeom.xs[i], y = elevGeom.bottom - (p.pos[1] - elevGeom.minY) * elevGeom.yScale;
    if (Math.hypot(sx - x, sy - y) <= 12) return idx;
  }
  return -1;
}
// Nearest profile sample to a given arc-length screen x -- shared by roll hit
// testing, dragging, and insertion so they all agree on the x <-> t mapping.
function sampleAtArc(sx) {
  const { padX, w, totalArc, profile } = elevGeom;
  if (!profile) return null;
  const arc = Math.max(0, Math.min(totalArc, (sx - padX) / (w - 2 * padX) * totalArc));
  let best = profile[0], bestD = Infinity, bestI = 0;
  profile.forEach((p, i) => { const d = Math.abs(p.arc - arc); if (d < bestD) { bestD = d; best = p; bestI = i; } });
  return { sample: best, t: elevGeom.totalSamples ? bestI / elevGeom.totalSamples : 0 };
}
function rollHandleAtElev(sx, sy) {
  return null;
}
// Add a new roll control point at the clicked elevation-strip screen position.
function insertRollPoint(sx, sy) {
  const path = curPath();
  if (!path || !elevGeom.profile) return;
  const hitT = sampleAtArc(sx);
  if (!hitT) return;
  pushUndo();
  const { top, bottom, minY, yScale, rollK } = elevGeom;
  const trackY = bottom - (hitT.sample.y - minY) * yScale;
  const clampedSy = Math.max(top + ROLL_HANDLE_MARGIN, Math.min(bottom - ROLL_HANDLE_MARGIN, sy));
  const roll = Math.max(-180, Math.min(180, Math.round(((trackY - clampedSy) / rollK) * 10) / 10));
  const rp = { type: 'roll', t: hitT.t, roll };
  path.points.push(rp);
  rollSel = rp; widthSel = null;
  dragging = 'rollElev';
  refresh();
}
// Add a new width control point on the currently-selected path at a given
// world position (top-down view), taking its initial width from the path's
// own width spline there so it doesn't visually jump.
function insertWidthPoint(worldX, worldZ) {
  const path = curPath();
  const prev = pathPreviews[sel.path];
  if (!path || !prev) return;
  pushUndo();
  const frames = prev.frames;
  let bestI = 0, bestD = Infinity;
  frames.forEach((f, i) => {
    const d = (f.pos.x - worldX) ** 2 + (f.pos.z - worldZ) ** 2;
    if (d < bestD) { bestD = d; bestI = i; }
  });
  const N = frames.length;
  const t = path.closed ? bestI / N : bestI / (N - 1);
  const wp = { type: 'width', t, width: +frames[bestI].width.toFixed(1) };
  path.points.push(wp);
  widthSel = wp; rollSel = null; crossSectionSel = null;
  dragging = 'widthTop';
  refresh();
}
// Add a new roll control point on the currently-selected path at a given
// world position (top-down view), mirroring insertWidthPoint.
function insertRollPointAtWorld(worldX, worldZ) {
  const path = curPath();
  const prev = pathPreviews[sel.path];
  if (!path || !prev) return;
  pushUndo();
  const frames = prev.frames;
  let bestI = 0, bestD = Infinity;
  frames.forEach((f, i) => {
    const d = (f.pos.x - worldX) ** 2 + (f.pos.z - worldZ) ** 2;
    if (d < bestD) { bestD = d; bestI = i; }
  });
  const N = frames.length;
  const t = path.closed ? bestI / N : bestI / (N - 1);
  const rp = { type: 'roll', t, roll: +(frames[bestI].roll * 180 / Math.PI).toFixed(1) };
  path.points.push(rp);
  rollSel = rp; widthSel = null; crossSectionSel = null;
  dragging = 'rollTop';
  refresh();
}
function insertCrossSectionPoint(worldX, worldZ) {
  const path = curPath();
  const prev = pathPreviews[sel.path];
  if (!path || !prev) return;
  pushUndo();
  const frames = prev.frames;
  let bestI = 0, bestD = Infinity;
  frames.forEach((f, i) => {
    const d = (f.pos.x - worldX) ** 2 + (f.pos.z - worldZ) ** 2;
    if (d < bestD) { bestD = d; bestI = i; }
  });
  const N = frames.length;
  const t = path.closed ? bestI / N : bestI / (N - 1);
  const cp = crossSectionPoint(t, +(frames[bestI].crossSectionCurvature || 0).toFixed(2), +(frames[bestI].crossSectionTightness || 1).toFixed(1), +(frames[bestI].crossSectionThickness || 0).toFixed(1));
  path.points.push(cp);
  crossSectionSel = cp; rollSel = null; widthSel = null;
  dragging = 'crossSectionTop';
  refresh();
}

// ---------- Insert a position point into the nearest segment (of the nearest
// path) at a clicked position ----------
function insertPositionAtSide(sx, sy) {
  const path = curPath();
  if (!path || !elevGeom.profile) return;
  const hit = sampleAtArc(sx);
  if (!hit) return;
  pushUndo();
  const pp = curParts();
  const { evalTrack, CP_N } = TrackCore.makeEvaluator(pp.controlPoints, path.closed, pp.rollPoints, pp.widthPoints, pp.crossSectionPoints);
  const gMax = path.closed ? CP_N : CP_N - 1;
  const g = hit.t * gMax;
  const s = evalTrack(Math.min(g, path.closed ? CP_N - 1e-6 : CP_N - 1));
  const y = elevGeom.minY + (elevGeom.bottom - sy) / elevGeom.yScale;
  const insertAt = path.closed
    ? (Math.floor(g) + 1) % (pp.controlPoints.length + 1)
    : Math.min(pp.controlPoints.length, Math.floor(g) + 1);
  const np = { type: 'position', id: newId('p'), pos: [+s.pos.x.toFixed(1), +y.toFixed(1), +s.pos.z.toFixed(1)], weight: 1 };
  insertPositionAt(path, insertAt, np);
  selectPosition(sel.path, insertAt);
  segSel = null; rollSel = null; widthSel = null; crossSectionSel = null;
  refresh();
}

function insertNear(worldX, worldZ) {
  pushUndo();
  let bestPath = 0, bestG = 0, bestD = Infinity;
  track.paths.forEach((path, pi) => {
    const pp = parts(path);
    const { evalTrack, CP_N } = TrackCore.makeEvaluator(pp.controlPoints, path.closed, pp.rollPoints, pp.widthPoints, pp.crossSectionPoints);
    const STEPS = CP_N * 24;
    const gMax = path.closed ? CP_N : CP_N - 1;
    for (let i = 0; i <= STEPS; i++) {
      const g = (i / STEPS) * gMax;
      const s = evalTrack(g);
      const d = (s.pos.x - worldX) ** 2 + (s.pos.z - worldZ) ** 2;
      if (d < bestD) { bestD = d; bestG = g; bestPath = pi; }
    }
  });
  const path = track.paths[bestPath];
  const pp = parts(path);
  const { evalTrack, CP_N } = TrackCore.makeEvaluator(pp.controlPoints, path.closed, pp.rollPoints, pp.widthPoints, pp.crossSectionPoints);
  const s = evalTrack(Math.min(bestG, path.closed ? CP_N - 1e-6 : CP_N - 1));
  const insertAt = path.closed
    ? (Math.floor(bestG) + 1) % (pp.controlPoints.length + 1)
    : Math.min(pp.controlPoints.length, Math.floor(bestG) + 1);
  const np = { type: 'position', id: newId('p'), pos: [worldX, s.pos.y, worldZ], weight: 1 };
  insertPositionAt(path, insertAt, np);
  selectPosition(bestPath, insertAt);
  rollSel = null; widthSel = null; crossSectionSel = null;
}

// Shift-dragging an open curve's endpoint out into empty space (no drop
// target under the cursor) extends that same curve with a brand new point
// there, instead of connecting to something that already exists. Inherits
// the dragged endpoint's elevation (there's no on-curve sample to inherit
// from beyond the curve's own end); drag it in the elevation view afterward
// to change that.
function extendCurveFromDrag(from, screenPos) {
  pushUndo();
  const path = track.paths[from.path];
  const cps = parts(path).controlPoints;
  const fromPoint = cps[from.point];
  const w = snapWorldXZ(screenToWorld(screenPos.x, screenPos.y));
  const np = {
    type: 'position', id: newId('p'),
    pos: [Math.round(w.x * 10) / 10, fromPoint.pos[1], Math.round(w.z * 10) / 10],
    weight: 1
  };
  const insertAt = from.end === 'start' ? 0 : cps.length;
  insertPositionAt(path, insertAt, np);
  selectPosition(from.path, insertAt);
  segSel = null; rollSel = null; widthSel = null; crossSectionSel = null;
  refresh();
}

function selectedOutgoingSegment() {
  if (rollSel || widthSel || crossSectionSel) return null;
  const path = curPath();
  if (!path) return null;
  const cps = curParts().controlPoints;
  if (path.closed) return { path: sel.path, i: sel.point };
  if (sel.point < cps.length - 1) return { path: sel.path, i: sel.point };
  return null;
}
function selectedIncomingSegment() {
  if (rollSel || widthSel || crossSectionSel) return null;
  const path = curPath();
  if (!path) return null;
  const cps = curParts().controlPoints;
  if (path.closed) return { path: sel.path, i: (sel.point - 1 + cps.length) % cps.length };
  if (sel.point > 0) return { path: sel.path, i: sel.point - 1 };
  return null;
}
function deleteSelectedSegment(which = 'outgoing') {
  const seg = segSel || (which === 'incoming' ? selectedIncomingSegment() : selectedOutgoingSegment());
  if (!seg) { alert(which === 'incoming' ? 'Select a point with a previous segment to delete.' : 'Select a point with a following segment to delete.'); return; }
  deleteSegment(seg.path, seg.i);
}

function deleteSelected() {
  const oldStartPoint = startPointObject(), oldReverse = !!(track.start && track.start.reverse);
  const path = curPath();
  if (!path) return;
  const pp = parts(path);
  if (countPointOccurrences(curPoint()) > 1) { alert('Reconnect this shared/disjoint point before deleting it.'); return; }
  if (pp.controlPoints.length <= 4) { alert('A track path needs at least 4 points.'); return; }
  pushUndo();
  const idxs = positionIndices(path);
  path.points.splice(idxs[sel.point], 1);
  selectPosition(sel.path, Math.max(0, sel.point - 1));
  preserveStartPoint(oldStartPoint, oldReverse);
  assertNoStaleSeams();
  segSel = null;
  refresh();
}

// ---------- Delete a segment: breaks a closed path open, shrinks/splits an
// open one ----------
function deleteSegment(pi, i) {
  const oldStartPoint = startPointObject(), oldReverse = !!(track.start && track.start.reverse);
  const path = track.paths[pi];
  if (pathHasDisjointSeam(path)) { alert('Reconnect disjoint corners on this path before deleting segments.'); return; }
  const idxs = positionIndices(path);
  const n = idxs.length;
  if (path.closed) {
    // Break the loop open at edge i -> i+1: rotate so the curve starts right
    // after the cut, keeping every point (position AND roll/width, which stay
    // wherever they are in the array -- only relative POSITION order changes)
    // and their cyclic order.
    pushUndo();
    const cut = (i + 1) % n;
    const posObjs = idxs.map(k => path.points[k]);
    const rotated = posObjs.slice(cut).concat(posObjs.slice(0, cut));
    idxs.forEach((arrIdx, k) => { path.points[arrIdx] = rotated[k]; });
    path.closed = false;
  } else if (i === 0) {
    if (n - 1 < 4) { alert('Path needs at least 4 control points; cannot shorten further.'); return; }
    pushUndo();
    path.points.splice(idxs[0], 1);
  } else if (i === n - 2) {
    if (n - 1 < 4) { alert('Path needs at least 4 control points; cannot shorten further.'); return; }
    pushUndo();
    path.points.splice(idxs[n - 1], 1);
  } else {
    const posObjs = idxs.map(k => path.points[k]);
    const left = posObjs.slice(0, i + 1), right = posObjs.slice(i + 1);
    if (left.length < 4 || right.length < 4) { alert("Splitting here would leave a path with fewer than 4 control points."); return; }
    // Fresh (flat) roll/width points for both halves -- the old splines don't
    // map cleanly onto the split domains, so this is redone by hand.
    pushUndo();
    track.paths.splice(pi, 1,
      { id: newId('path'), closed: false, points: left.concat(flatRollWidthDefaults(false)) },
      { id: newId('path'), closed: false, points: right.concat(flatRollWidthDefaults(false)) });
  }
  preserveStartPoint(oldStartPoint, oldReverse);
  assertNoStaleSeams();
  segSel = null; joinSel = []; rollSel = null; widthSel = null; crossSectionSel = null; syncSelectionToId();
  refresh();
}

// ---------- Join two open-path endpoints: closes a path, or merges two paths
// into one open path ----------
function replacePositionOccurrence(path, pointIndex, replacement) {
  const idxs = positionIndices(path);
  path.points[idxs[pointIndex]] = replacement;
}
function splitTargetPathAt(pathIndex, pointIndex) {
  const path = track.paths[pathIndex];
  const cps = parts(path).controlPoints;
  const pp = parts(path);
  if (path.closed) {
    // Branching into the middle of a closed loop must open the loop at the
    // junction. Otherwise the closed loop's rails remain continuous through the
    // branch intersection (the visual bug in the game screenshot).
    const n = cps.length;
    const rotated = cps.slice(pointIndex).concat(cps.slice(0, pointIndex), cps[pointIndex]);
    const startT = pointIndex / n;
    track.paths.splice(pathIndex, 1, {
      id: path.id || newId('path'),
      closed: false,
      points: rotated.concat(rollWidthForSourceRange(pp, true, startT, startT + 1))
    });
    return true;
  }
  if (pointIndex <= 0 || pointIndex >= cps.length - 1) return;
  if (pointIndex + 1 < 4 || cps.length - pointIndex < 4) { alert('Target split would leave a curve with fewer than 4 points.'); return false; }
  const max = Math.max(1, cps.length - 1);
  const left = cps.slice(0, pointIndex + 1);
  const right = cps.slice(pointIndex);
  track.paths.splice(pathIndex, 1,
    { id: newId('path'), closed: false, points: left.concat(rollWidthForSourceRange(pp, false, 0, pointIndex / max)) },
    { id: newId('path'), closed: false, points: right.concat(rollWidthForSourceRange(pp, false, pointIndex / max, 1)) }
  );
  return true;
}
function deleteSelectedCurve() {
  if (track.paths.length <= 1) { alert('A track needs at least one curve.'); return; }
  syncSelectionToId();
  const deleteIndex = sel.path;
  pushUndo();
  track.paths.splice(deleteIndex, 1);
  if (track.start) {
    if (track.start.path === deleteIndex) track.start = { path: Math.max(0, Math.min(deleteIndex, track.paths.length - 1)), point: 0, reverse: !!track.start.reverse };
    else if (track.start.path > deleteIndex) track.start.path--;
  }
  removeStaleSeams();
  selectedPointId = null;
  sel.path = Math.max(0, Math.min(deleteIndex, track.paths.length - 1));
  syncSelectionToId();
  clampStart();
  segSel = null; joinSel = []; rollSel = null; widthSel = null; crossSectionSel = null;
  refresh();
}

function performJoin() {
  if (joinSel.length !== 2) return;
  const oldStartPoint = startPointObject(), oldReverse = !!(track.start && track.start.reverse);
  let [a, b] = joinSel;
  if (pathHasDisjointSeam(track.paths[a.path]) || pathHasDisjointSeam(track.paths[b.path])) {
    alert('Reconnect disjoint corners before connecting paths.'); return;
  }
  if (a.path === b.path) {
    if (!a.end || !b.end || a.end === b.end) { alert('Pick the two different open ends to close this curve.'); return; }
    pushUndo();
    track.paths[a.path].closed = true;
  } else {
    const aEndpoint = !!a.end, bEndpoint = !!b.end;
    if (!aEndpoint && !bEndpoint) { alert('One selected point must be an open curve endpoint.'); return; }
    pushUndo();
    const source = aEndpoint ? a : b;
    const target = aEndpoint ? b : a;
    const sourcePath = track.paths[source.path], targetPath = track.paths[target.path];
    const targetPoint = parts(targetPath).controlPoints[target.point];
    replacePositionOccurrence(sourcePath, source.point, targetPoint);
    if (target.point > 0 && target.point < parts(targetPath).controlPoints.length - 1) {
      const ok = splitTargetPathAt(target.path, target.point);
      if (ok === false) return;
    }
    if (!track.junctions) track.junctions = [];
    track.junctions.push({ id: newId('j'), pointId: targetPoint.id, sourcePathId: sourcePath.id, sourceEnd: source.end, targetPathId: targetPath.id });
    selectedPointId = targetPoint.id;
  }
  preserveStartPoint(oldStartPoint, oldReverse);
  assertNoStaleSeams();
  joinSel = []; segSel = null; rollSel = null; widthSel = null; crossSectionSel = null; syncSelectionToId();
  refresh();
}

// ---------- Add-point popup menu (top-down view) ----------
const addPointMenu = document.getElementById('addPointMenu');
const pasteMeshSection = document.getElementById('pasteMeshSection');
let pendingAdd = null; // { worldX, worldZ }
// Each menu opening gets a token so a slow clipboard read from a previous,
// already-dismissed menu can't reveal the paste option on the current one.
let menuToken = 0;

function showAddPointMenu(clientX, clientY, worldX, worldZ) {
  pendingAdd = { worldX, worldZ };
  addPointMenu.style.left = Math.min(clientX, window.innerWidth - 140) + 'px';
  addPointMenu.style.top = Math.min(clientY, window.innerHeight - 120) + 'px';
  addPointMenu.style.display = 'flex';
  // The menu opens immediately with the paste option hidden, then reveals it
  // if the clipboard turns out to hold something. Awaiting the check first
  // would stall the menu behind it on every right-click.
  pasteMeshSection.style.display = 'none';
  const token = ++menuToken;
  clipboardMayHaveText().then((has) => {
    if (!has || token !== menuToken || addPointMenu.style.display === 'none') return;
    pasteMeshSection.style.display = 'flex';
  });
}
function hideAddPointMenu() {
  addPointMenu.style.display = 'none';
  pasteMeshSection.style.display = 'none';
  menuToken++;
  pendingAdd = null;
}

// Whether to offer "From clipboard" at all.
//
// The obvious implementation -- just readText() and see -- is wrong, because
// an ungranted read pops the browser's own native "Paste" confirmation
// bubble. Doing that merely because someone opened a context menu means a
// system popup they never asked for, layered over ours, on EVERY right-click;
// and since the real paste reads again, they get a second one. So consult the
// permission first, which is silent:
//
//   granted -> reading is silent, so probe properly and only offer the option
//              when there is really something there.
//   denied  -> the paste could never work, so omit it.
//   prompt  -> offer it unprobed. The single read then happens on the click
//              that actually asks to paste, where a confirmation is expected,
//              and an empty clipboard is reported gracefully by parseMeshJSON.
//
// Whether the text is actually a mesh is left to import time either way, which
// reports a specific reason -- probing that hard here would mean parsing on
// every right-click.
async function clipboardMayHaveText() {
  if (!navigator.clipboard?.readText) return false;
  let state = 'prompt';
  try {
    state = (await navigator.permissions.query({ name: 'clipboard-read' })).state;
  } catch { state = 'prompt'; }   // no Permissions API, or the name is unknown (Firefox)
  if (state === 'denied') return false;
  if (state !== 'granted') return true;
  try {
    return !!(await navigator.clipboard.readText()).trim();
  } catch { return false; }
}

// The right mouse button's own 'contextmenu' event fires *after* mousedown,
// by which point this popup is already the frontmost element under the
// cursor -- so the OS/browser hit-tests it, not topCanvas underneath, and
// topCanvas's own contextmenu handler (which only suppresses its own
// bubbling) never runs. Without this, the browser's native menu (Copy /
// Paste / Inspect, depending on browser) appears layered on top of ours.
addPointMenu.addEventListener('contextmenu', (e) => e.preventDefault());

addPointMenu.addEventListener('click', (e) => {
  if (!pendingAdd) return;
  const { worldX, worldZ } = pendingAdd;
  if (e.target.dataset.action === 'pasteMesh') {
    hideAddPointMenu();
    importMeshFromClipboard({ x: worldX, z: worldZ });
    return;
  }
  const type = e.target.dataset.type;
  if (!type) return;
  hideAddPointMenu();
  segSel = null;
  if (type === 'position') insertNear(worldX, worldZ);
  else if (type === 'width') insertWidthPoint(worldX, worldZ);
  else if (type === 'crossSection') insertCrossSectionPoint(worldX, worldZ);
  else insertRollPointAtWorld(worldX, worldZ);
  dragging = null;
  refresh();
});
window.addEventListener('mousedown', (e) => {
  if (addPointMenu.style.display !== 'none' && !addPointMenu.contains(e.target)) hideAddPointMenu();
}, true);

// ---------- Mode switching ----------
// The single entry point for changing mode, so the dropdown and the E/C/R
// shortcuts can never disagree about the abandoned draft or the rail pick.
function setEditMode(mode) {
  editMode = mode;
  createDraft = [];
  // The picked-edge highlight only means anything inside Rails mode.
  if (editMode !== 'rails') railSel = null;
  const modeEl = document.getElementById('editModeSelect');
  if (modeEl) modeEl.value = mode;
  hideAddPointMenu();
  draw();
}

// ---------- Create mode ----------
function finishCreateDraft(closed) {
  if (createDraft.length < 4) { alert('A curve needs at least 4 position points.'); return; }
  pushUndo();
  const path = { id: newId('path'), closed, points: createDraft.slice().concat(flatRollWidthDefaults(closed)) };
  track.paths.push(path);
  selectPosition(track.paths.length - 1, closed ? 0 : createDraft.length - 1);
  setEditMode('edit');
  refresh();
}
// Hit-test against the in-progress draft's own points (screen-space, same
// radius as nodeAtTop). Draft points aren't part of track.paths yet, so
// nodeAtTop -- which only searches existing paths -- can't see them; without
// this, the first/last draft point is drawn but not actually clickable.
function draftNodeAt(sx, sy) {
  for (let i = 0; i < createDraft.length; i++) {
    const s = worldToScreen(createDraft[i].pos[0], createDraft[i].pos[2]);
    if (Math.hypot(sx - s.x, sy - s.y) <= 10) return i;
  }
  return -1;
}
function createModeClick(x, y) {
  const draftIdx = draftNodeAt(x, y);
  if (draftIdx >= 0 && createDraft.length) {
    if (draftIdx === 0) { finishCreateDraft(true); return; }
    if (draftIdx === createDraft.length - 1) { finishCreateDraft(false); return; }
    alert('Repeated points are only allowed to finish the curve.');
    return;
  }
  const hit = nodeAtTop(x, y);
  let p = hit ? pathPreviews[hit.path].parts.controlPoints[hit.point] : null;
  if (p && createDraft.length) {
    if (p === createDraft[0]) { finishCreateDraft(true); return; }
    if (p === createDraft[createDraft.length - 1]) { finishCreateDraft(false); return; }
    if (createDraft.includes(p)) { alert('Repeated points are only allowed to finish the curve.'); return; }
  }
  if (!p) {
    const w = snapWorldXZ(screenToWorld(x, y));
    p = { type: 'position', id: newId('p'), pos: [Math.round(w.x * 10) / 10, 0, Math.round(w.z * 10) / 10], weight: 1 };
  }
  createDraft.push(p);
  draw();
}

// ---------- Top-down mouse ----------
function localPos(canvas, e) {
  const r = canvas.getBoundingClientRect();
  return { x: e.clientX - r.left, y: e.clientY - r.top };
}
// 'start'/'end' if hit is an open path's endpoint (eligible for Join), else null.
function endpointEndFor(hit) {
  const path = track.paths[hit.path];
  const cps = pathPreviews[hit.path].parts.controlPoints;
  const isEndpoint = !path.closed && (hit.point === 0 || hit.point === cps.length - 1);
  return isEndpoint ? (hit.point === 0 ? 'start' : 'end') : null;
}
topCanvas.addEventListener('mousedown', (e) => {
  const { x, y } = localPos(topCanvas, e);
  // Rails mode is modal on purpose: only mesh edges are pickable, so flagging
  // a rail can never be confused with selecting or dragging anything else.
  if (editMode === 'rails') {
    e.preventDefault();
    hideAddPointMenu();
    if (e.button !== 0) return;
    const w = screenToWorld(x, y);
    const edgeHit = meshEdgeAtWorld(w.x, w.z, MESH_EDGE_PICK_PX / view.scale);
    if (edgeHit) {
      const mesh = assetMesh(edgeHit.assetId);
      if (mesh) {
        pushUndo();
        TrackMesh.toggleRailEdge(mesh, edgeHit.edgeId);
        writeBackAsset(edgeHit.assetId);
        selectedMeshId = edgeHit.meshId;
        railSel = { meshId: edgeHit.meshId, edgeId: edgeHit.edgeId };
        updateUndoRedoButtons();
        refresh();
        return;
      }
    }
    railSel = null;
    dragging = 'panTop'; panLast = { x, y }; topPanned = false;
    refresh();
    return;
  }
  if (editMode === 'create') {
    e.preventDefault();
    hideAddPointMenu();
    if (e.button === 2) { createDraft = []; draw(); return; }
    if (e.button === 0) createModeClick(x, y);
    return;
  }
  if (e.button === 2) {
    e.preventDefault();
    const w = snapWorldXZ(screenToWorld(x, y));
    showAddPointMenu(e.clientX, e.clientY, w.x, w.z);
    return;
  }
  if (e.button !== 0) return;
  const crossSectionHandleHit = crossSectionHandleAtTop(x, y);
  if (crossSectionHandleHit && !e.shiftKey) { dragMutated = false; crossSectionSel = crossSectionHandleHit; widthSel = null; rollSel = null; segSel = null; physicsSel = null; dragging = 'crossSectionTop'; refresh(); return; }
  const widthHandleHit = widthHandleAtTop(x, y);
  if (widthHandleHit && !e.shiftKey) { dragMutated = false; widthSel = widthHandleHit; rollSel = null; crossSectionSel = null; segSel = null; physicsSel = null; dragging = 'widthTop'; refresh(); return; }
  const rollHandleHit = rollHandleAtTop(x, y);
  if (rollHandleHit && !e.shiftKey) { dragMutated = false; rollSel = rollHandleHit; widthSel = null; crossSectionSel = null; segSel = null; physicsSel = null; dragging = 'rollTop'; refresh(); return; }
  const rollHit = rollNodeAtTop(x, y);
  if (rollHit && !e.shiftKey) { rollSel = rollHit; widthSel = null; segSel = null; physicsSel = null; refresh(); return; }
  if (!e.shiftKey) {
    const crossingHit = crossingMarkerAtTop(x, y);
    if (crossingHit) { cycleCrossingOverride(crossingHit); return; }
  }
  const hit = nodeAtTop(x, y);
  if (hit && e.shiftKey) {
    const end = endpointEndFor(hit);
    if (end) {
      // Shift-dragging an open curve's endpoint: drop it onto another point to
      // connect them (see the 'joinDrag' mousemove/mouseup handling below).
      // Non-endpoint hits fall through to the click-to-multi-select flow.
      joinDragFrom = { path: hit.path, point: hit.point, end };
      joinDragTarget = null;
      joinDragScreen = { x, y };
      joinDragStartScreen = { x, y };
      dragging = 'joinDrag';
      refresh();
      return;
    }
    const already = joinSel.findIndex(j => j.path === hit.path && j.point === hit.point);
    if (already >= 0) joinSel.splice(already, 1);
    else { joinSel.push({ path: hit.path, point: hit.point, end }); if (joinSel.length > 2) joinSel.shift(); }
    refresh();
    return;
  }
  if (hit) { dragMutated = false; selectPosition(hit.path, hit.point); segSel = null; rollSel = null; widthSel = null; crossSectionSel = null; physicsSel = null; dragging = 'top'; refresh(); return; }
  // Physics sample points (debug overlay) are picked after authored control
  // points so editing is never obstructed, but before mesh regions. They are
  // read-only: select to inspect, no drag.
  const physHit = physicsPointAtTop(x, y);
  if (physHit && !e.shiftKey) {
    physicsSel = physHit;
    selectedPointId = null; segSel = null; rollSel = null; widthSel = null; crossSectionSel = null; clearMeshSelection();
    hideAddPointMenu();
    refresh();
    return;
  }
  // Mesh regions are picked last, after every path handle: they are large
  // targets and must never steal a click from a control point drawn on top.
  {
    const w = screenToWorld(x, y);
    const meshHit = meshAtWorld(w.x, w.z);
    if (meshHit) {
      dragMutated = false;
      selectedPointId = null; segSel = null; rollSel = null; widthSel = null; crossSectionSel = null; physicsSel = null;
      selectedMeshId = meshHit.placement.id;
      railSel = null;
      if (e.shiftKey) {
        // Angle is measured the same way TrackMesh.localToWorld measures
        // `rotation`: atan2(dz, dx) from the placement origin. Recording the
        // offset between that and the mouse's *current* angle -- rather than
        // snapping rotation straight to it -- means the shape doesn't jump the
        // moment the drag starts.
        const startAngle = Math.atan2(w.z - meshHit.placement.z, w.x - meshHit.placement.x) * 180 / Math.PI;
        meshRotateStart = { originRotation: meshHit.placement.rotation || 0, startAngle };
        dragging = 'meshRotate';
      } else {
        meshDragOffset = { dx: meshHit.placement.x - w.x, dz: meshHit.placement.z - w.z };
        dragging = 'meshTop';
      }
      hideAddPointMenu();
      refresh();
      return;
    }
  }
  hideAddPointMenu();
  dragging = 'panTop';
  panLast = { x, y };
  topPanned = false;
});
topCanvas.addEventListener('contextmenu', (e) => {
  e.preventDefault();
});
window.addEventListener('mousemove', (e) => {
  if (dragging === 'panTop') {
    const { x, y } = localPos(topCanvas, e);
    if (panLast) {
      const dx = x - panLast.x, dy = y - panLast.y;
      if (Math.hypot(dx, dy) > 0) topPanned = true;
      topPan.x += dx; topPan.y += dy;
      panLast = { x, y };
      draw();
    }
  } else if (dragging === 'meshTop') {
    const placement = selectedPlacement();
    if (placement && meshDragOffset) {
      freezeTopViewForDrag();
      if (!dragMutated) { pushUndo(); dragMutated = true; updateUndoRedoButtons(); }
      const { x, y } = localPos(topCanvas, e);
      const w = screenToWorld(x, y);
      const moved = snapWorldXZ({ x: w.x + meshDragOffset.dx, z: w.z + meshDragOffset.dz });
      placement.x = Math.round(moved.x * 10) / 10;
      placement.z = Math.round(moved.z * 10) / 10;
      refresh();
    }
  } else if (dragging === 'meshRotate') {
    const placement = selectedPlacement();
    if (placement && meshRotateStart) {
      freezeTopViewForDrag();
      if (!dragMutated) { pushUndo(); dragMutated = true; updateUndoRedoButtons(); }
      const { x, y } = localPos(topCanvas, e);
      const w = screenToWorld(x, y);
      const angle = Math.atan2(w.z - placement.z, w.x - placement.x) * 180 / Math.PI;
      let rotation = meshRotateStart.originRotation + (angle - meshRotateStart.startAngle);
      rotation = ((rotation % 360) + 360) % 360;
      placement.rotation = Math.round(rotation * 10) / 10;
      refresh();
    }
  } else if (dragging === 'meshElev') {
    const placement = selectedPlacement();
    if (placement) {
      if (!dragMutated) { pushUndo(); dragMutated = true; updateUndoRedoButtons(); }
      const { y } = localPos(elevCanvas, e);
      const val = elevGeom.minY + (elevGeom.bottom - y) / elevGeom.yScale;
      placement.elevation = Math.round(val * 10) / 10;
      refresh();
    }
  } else if (dragging === 'top') {
    freezeTopViewForDrag();
    if (!dragMutated) { pushUndo(); dragMutated = true; }
    const { x, y } = localPos(topCanvas, e);
    const w = snapWorldXZ(screenToWorld(x, y));
    curPoint().pos[0] = Math.round(w.x * 10) / 10;
    curPoint().pos[2] = Math.round(w.z * 10) / 10;
    refresh();
  } else if (dragging === 'elev') {
    freezeTopViewForDrag();
    if (!dragMutated) { pushUndo(); dragMutated = true; }
    const { y } = localPos(elevCanvas, e);
    const val = elevGeom.minY + (elevGeom.bottom - y) / elevGeom.yScale;
    curPoint().pos[1] = Math.round(val * 10) / 10;
    refresh();
  } else if (dragging === 'rollElev') {
    if (rollSel) {
      const { x, y } = localPos(elevCanvas, e);
      const hit = sampleAtArc(x);
      if (hit) {
        if (!dragMutated) { pushUndo(); dragMutated = true; }
        const trackY = elevGeom.bottom - (hit.sample.y - elevGeom.minY) * elevGeom.yScale;
        const clampedY = Math.max(elevGeom.top + ROLL_HANDLE_MARGIN, Math.min(elevGeom.bottom - ROLL_HANDLE_MARGIN, y));
        rollSel.t = hit.t;
        rollSel.roll = Math.max(-180, Math.min(180, Math.round(((trackY - clampedY) / elevGeom.rollK) * 10) / 10));
        refresh();
      }
    }
  } else if (dragging === 'rollTop') {
    const path = curPath();
    const prev = pathPreviews[sel.path];
    if (path && prev && rollSel) {
      if (!dragMutated) { pushUndo(); dragMutated = true; }
      const { x, y } = localPos(topCanvas, e);
      const w = screenToWorld(x, y);
      const f = frameAtT(prev.frames, path.closed, rollSel.t);
      // signed distance of the mouse from the roll point, projected onto the
      // track's perpendicular (+h = right) -- inverse of rollLineEnd()'s length
      const dist = (w.x - f.pos.x) * f.h.x + (w.z - f.pos.z) * f.h.z;
      const roll = f.width > 0 ? (dist / f.width) * 180 : 0;
      rollSel.roll = Math.max(-180, Math.min(180, Math.round(roll * 10) / 10));
      refresh();
    }
  } else if (dragging === 'crossSectionTop') {
    const path = curPath();
    const prev = pathPreviews[sel.path];
    if (path && prev && crossSectionSel) {
      if (!dragMutated) { pushUndo(); dragMutated = true; }
      const { x, y } = localPos(topCanvas, e);
      const w = screenToWorld(x, y);
      const f = frameAtT(prev.frames, path.closed, crossSectionSel.t);
      const dist = (w.x - f.pos.x) * f.h.x + (w.z - f.pos.z) * f.h.z;
      const curvature = f.width > 0 ? dist / (f.width / 2) : 0;
      crossSectionSel.curvature = Math.max(-1, Math.min(1, Math.round(curvature * 100) / 100));
      refresh();
    }
  } else if (dragging === 'widthTop') {
    const path = curPath();
    const prev = pathPreviews[sel.path];
    if (path && prev && widthSel) {
      if (!dragMutated) { pushUndo(); dragMutated = true; }
      const { x, y } = localPos(topCanvas, e);
      const w = screenToWorld(x, y);
      const f = frameAtT(prev.frames, path.closed, widthSel.t);
      // distance of the mouse from the width point's centerline position,
      // projected onto +-h -- either edge handle sits at halfW along h, so
      // |distance|*2 = full width regardless of which handle is dragged.
      const dist = (w.x - f.pos.x) * f.h.x + (w.z - f.pos.z) * f.h.z;
      widthSel.width = Math.max(1, Math.round(Math.abs(dist) * 2 * 10) / 10);
      refresh();
    }
  } else if (dragging === 'joinDrag') {
    const { x, y } = localPos(topCanvas, e);
    joinDragScreen = { x, y };
    const hit = nodeAtTop(x, y);
    joinDragTarget = (hit && !(hit.path === joinDragFrom.path && hit.point === joinDragFrom.point)) ? hit : null;
    draw();
  }
});
window.addEventListener('mouseup', () => {
  if (dragging === 'joinDrag') {
    if (joinDragFrom && joinDragTarget) {
      joinSel = [joinDragFrom, { path: joinDragTarget.path, point: joinDragTarget.point, end: endpointEndFor(joinDragTarget) }];
      performJoin(); // validates the pair, connects them, and calls refresh()
    } else if (joinDragFrom && joinDragScreen && joinDragStartScreen) {
      const dragDist = Math.hypot(joinDragScreen.x - joinDragStartScreen.x, joinDragScreen.y - joinDragStartScreen.y);
      // Ignore a plain shift-click (no real drag) -- only an intentional drag
      // out into empty space extends the curve with a new point there.
      if (dragDist >= JOIN_DRAG_MIN_PX) extendCurveFromDrag(joinDragFrom, joinDragScreen);
    }
    joinDragFrom = null; joinDragTarget = null; joinDragScreen = null; joinDragStartScreen = null;
    draw();
  }
  releaseTopViewFreeze();
  dragging = null; panLast = null; dragMutated = false; meshDragOffset = null; meshRotateStart = null;
});

// ---------- Elevation mouse ----------
elevCanvas.addEventListener('mousedown', (e) => {
  const { x, y } = localPos(elevCanvas, e);
  if (e.button === 2) {
    e.preventDefault();
    if (pointFilters.position && x >= elevGeom.padX && x <= elevGeom.w - elevGeom.padX) insertPositionAtSide(x, y);
    return;
  }
  if (e.button !== 0) return;
  // The selected mesh's elevation line spans the full panel width, so grab it
  // on vertical proximity alone.
  const meshPlacement = selectedPlacement();
  if (meshPlacement) {
    const my = elevGeom.bottom - (meshPlacement.elevation - elevGeom.minY) * elevGeom.yScale;
    if (Math.abs(y - my) <= MESH_ELEV_PICK_PX) {
      dragMutated = false; rollSel = null; widthSel = null; crossSectionSel = null;
      dragging = 'meshElev';
      refresh();
      return;
    }
  }
  const rollHit = rollHandleAtElev(x, y);
  if (rollHit) { dragMutated = false; rollSel = rollHit; widthSel = null; dragging = 'rollElev'; refresh(); return; }
  const hit = handleAtElev(x, y);
  if (hit >= 0) { dragMutated = false; selectPosition(sel.path, hit); rollSel = null; widthSel = null; crossSectionSel = null; dragging = 'elev'; refresh(); return; }
  // Roll control points are edited from the top-down view, not the side view.
});
elevCanvas.addEventListener('contextmenu', (e) => { e.preventDefault(); });

// ---------- Keyboard ----------
window.addEventListener('keydown', (e) => {
  if ((e.ctrlKey || e.metaKey) && !e.shiftKey && e.key.toLowerCase() === 'z') { e.preventDefault(); undo(); return; }
  if ((e.ctrlKey || e.metaKey) && (e.key.toLowerCase() === 'y' || (e.shiftKey && e.key.toLowerCase() === 'z'))) { e.preventDefault(); redo(); return; }
  if (e.target.tagName === 'INPUT') return;
  // A focused dropdown/textarea owns its own letter keys (native typeahead),
  // so don't shadow them -- including the mode dropdown itself, where
  // typeahead already reaches every mode and fires 'change'.
  if (e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
  // Mode shortcuts. Bare keys only: leave Ctrl/Alt/Meta chords to the browser.
  if (!e.ctrlKey && !e.metaKey && !e.altKey) {
    const mode = { KeyE: 'edit', KeyC: 'create', KeyR: 'rails' }[e.code];
    if (mode) { e.preventDefault(); setEditMode(mode); return; }
  }
  if (e.key === 'Delete' || e.key === 'Backspace') {
    e.preventDefault();
    if (selectedMeshId) { deleteSelectedMesh(); return; }
    if (crossSectionSel) {
      const path = curPath();
      if (parts(path).crossSectionPoints.length <= 2) { alert('A path needs at least 2 cross-section points.'); return; }
      pushUndo();
      path.points.splice(path.points.indexOf(crossSectionSel), 1);
      crossSectionSel = null;
      refresh();
    } else if (widthSel) {
      const path = curPath();
      if (parts(path).widthPoints.length <= 2) { alert('A path needs at least 2 width points.'); return; }
      pushUndo();
      path.points.splice(path.points.indexOf(widthSel), 1);
      widthSel = null;
      refresh();
    } else if (rollSel) {
      const path = curPath();
      if (parts(path).rollPoints.length <= 2) { alert('A path needs at least 2 roll points.'); return; }
      pushUndo();
      path.points.splice(path.points.indexOf(rollSel), 1);
      rollSel = null;
      refresh();
    } else deleteSelectedSegment();
  }
});

// ---------- File controls ----------
// Same one-push-per-typing-session pattern as the property panel's number
// fields (see renderProps' armHistory): typing fires 'input' per keystroke,
// so only the first keystroke since the field was last focused pushes undo.
let nameHistoryArmed = false;
const nameInputEl = document.getElementById('nameInput');
nameInputEl.addEventListener('input', (e) => {
  if (!nameHistoryArmed) { pushUndo(); nameHistoryArmed = true; updateUndoRedoButtons(); }
  track.name = e.target.value;
});
nameInputEl.addEventListener('blur', () => { nameHistoryArmed = false; });
document.getElementById('newBtn').addEventListener('click', () => {
  pushUndo();
  track = TrackCore.cloneTrack(TrackCore.STARTER_TRACK);
  zeroElevationAndRoll(track);
  ensureTrackIds();
  assertNoStaleSeams();
  selectedPointId = null; syncSelectionToId(); segSel = null; joinSel = []; rollSel = null; widthSel = null; crossSectionSel = null; topPan = { x: 0, y: 0 };
  clearMeshSelection();
  invalidateMeshCache();
  refresh();
});

// ---------- Random track generator ----------
// A "Random" button builds a fresh closed-loop track at the complexity the
// slider sets; the ranges the complexity scales within are configured in a
// small popup (#randomPanel) and persisted to localStorage. Determinism: a
// seeded PRNG (mulberry32) means a given seed+complexity always reproduces the
// same track -- each click rolls a new seed and shows it, and typing a seed
// back in (Enter/blur) rebuilds that exact one.
const RANDOM_RANGE_DEFAULTS = Object.freeze({
  lengthMin: 8000, lengthMax: 9000,
  turnsMin: 6, turnsMax: 22,
  maxBanking: 25, maxHill: 300,
  widthMin: 28, widthMax: 52, maxCurvature: 0.5
});
const RANDOM_RANGES_KEY = 'web3d.randomRanges';
function clampNum(v, lo, hi, d) { v = Number(v); return Number.isFinite(v) ? Math.max(lo, Math.min(hi, v)) : d; }
function sanitizeRandomRanges(r) {
  const d = RANDOM_RANGE_DEFAULTS;
  const out = {
    lengthMin: clampNum(r.lengthMin, 500, 100000, d.lengthMin),
    lengthMax: clampNum(r.lengthMax, 500, 100000, d.lengthMax),
    turnsMin: Math.round(clampNum(r.turnsMin, 4, 40, d.turnsMin)),
    turnsMax: Math.round(clampNum(r.turnsMax, 4, 40, d.turnsMax)),
    maxBanking: clampNum(r.maxBanking, 0, 60, d.maxBanking),
    maxHill: clampNum(r.maxHill, 0, 5000, d.maxHill),
    widthMin: clampNum(r.widthMin, 1, 2000, d.widthMin),
    widthMax: clampNum(r.widthMax, 1, 2000, d.widthMax),
    maxCurvature: clampNum(r.maxCurvature, 0, 1, d.maxCurvature)
  };
  // Keep each pair ordered so lerps behave, whatever the user typed.
  if (out.lengthMax < out.lengthMin) out.lengthMax = out.lengthMin;
  if (out.turnsMax < out.turnsMin) out.turnsMax = out.turnsMin;
  if (out.widthMax < out.widthMin) out.widthMax = out.widthMin;
  return out;
}
function loadRandomRanges() {
  try {
    const s = localStorage.getItem(RANDOM_RANGES_KEY);
    if (s) return sanitizeRandomRanges({ ...RANDOM_RANGE_DEFAULTS, ...JSON.parse(s) });
  } catch (e) { /* corrupt/blocked storage -> defaults */ }
  return { ...RANDOM_RANGE_DEFAULTS };
}
function saveRandomRanges() { try { localStorage.setItem(RANDOM_RANGES_KEY, JSON.stringify(randomRanges)); } catch (e) { /* ignore */ } }
let randomRanges = loadRandomRanges();

// Small deterministic PRNG: same seed -> same stream. Enough for track shape.
function mulberry32(a) {
  return function () {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// Driven length of a closed loop of bare control points (y ignored by passing
// them through as-is), used to calibrate a generated loop to a target length.
function measureLoopLength(controlPoints) {
  const frames = TrackCore.buildCenterline(controlPoints, 400, true);
  let len = 0;
  for (let i = 1; i < frames.length; i++) {
    const a = frames[i - 1].pos, b = frames[i].pos;
    len += Math.hypot(b.x - a.x, b.y - a.y, b.z - a.z);
  }
  const a = frames[frames.length - 1].pos, b = frames[0].pos;
  return len + Math.hypot(b.x - a.x, b.y - a.y, b.z - a.z);
}

function generateRandomTrack(complexity, seed, ranges) {
  const rnd = mulberry32(seed >>> 0);
  const t = (Math.max(1, Math.min(10, complexity)) - 1) / 9;   // 0..1
  const turnsMin = Math.max(4, Math.round(ranges.turnsMin));
  const turnsMax = Math.max(turnsMin, Math.round(ranges.turnsMax));
  const N = Math.max(4, Math.round(turnsMin + (turnsMax - turnsMin) * t));

  // Points at STRICTLY INCREASING angle around a centre -> guaranteed simple
  // (non-self-crossing) loop. Radius jitter (growing with complexity) is what
  // makes the turns; the angle jitter (< half the spacing) stays monotonic.
  const baseR = 1000, jitterAmp = 0.12 + 0.42 * t, spacing = (Math.PI * 2) / N;
  const xs = [], zs = [];
  for (let i = 0; i < N; i++) {
    const ang = i * spacing + (rnd() - 0.5) * 0.8 * spacing;
    const r = baseR * (1 + (rnd() - 0.5) * 2 * jitterAmp);
    xs.push(Math.cos(ang) * r); zs.push(Math.sin(ang) * r);
  }
  // Calibrate the horizontal size to a random target length within the band.
  const flatCps = xs.map((x, i) => ({ pos: [x, 0, zs[i]], weight: 1 }));
  const L0 = measureLoopLength(flatCps);
  const targetLen = ranges.lengthMin + (ranges.lengthMax - ranges.lengthMin) * rnd();
  const scale = L0 > 1e-6 ? targetLen / L0 : 1;
  for (let i = 0; i < N; i++) { xs[i] *= scale; zs[i] *= scale; }

  // Smooth rolling hills: a few low-frequency sinusoids around the loop, summed
  // and normalised, so elevation is continuous through the closed wrap. Applied
  // AFTER length calibration so maxHill stays a true world-unit cap.
  const nH = 2 + Math.floor(rnd() * 3), harm = [];
  for (let k = 0; k < nH; k++) harm.push({ freq: 1 + Math.floor(rnd() * 4), phase: rnd() * Math.PI * 2, amp: 0.4 + 0.6 * rnd() });
  const ampSum = harm.reduce((s, h) => s + h.amp, 0) || 1;
  const hillAmp = ranges.maxHill * t;
  const ys = [];
  for (let i = 0; i < N; i++) {
    const frac = i / N; let y = 0;
    for (const h of harm) y += h.amp * Math.sin(frac * Math.PI * 2 * h.freq + h.phase);
    ys.push((y / ampSum) * hillAmp);
  }
  // The hills add a few % of 3D length on top of the flat calibration, which
  // could push a track past the band. Correct with one final UNIFORM scale so
  // the true driven length lands in [lengthMin, lengthMax]; because it is
  // uniform it also keeps hill height at or just under maxHill (corr <= 1).
  const fullCps = xs.map((x, i) => ({ pos: [x, ys[i], zs[i]], weight: 1 }));
  const L3d = measureLoopLength(fullCps);
  const corr = L3d > 1e-6 ? targetLen / L3d : 1;
  for (let i = 0; i < N; i++) { xs[i] *= corr; zs[i] *= corr; ys[i] *= corr; }

  const width0 = TrackCore.DEFAULT_WIDTH;
  const points = [];
  for (let i = 0; i < N; i++) points.push({ type: 'position', pos: [xs[i], ys[i], zs[i]], weight: 1 });
  // Banking: lean INTO each corner. The signed turn metric
  // m = in_z*out_x - in_x*out_z is > 0 for a right-hand turn, which is exactly
  // the sign of +roll (which lifts the LEFT edge -> banks into a right turn)
  // per track-core.js. Magnitude from turn sharpness (asin m), capped at
  // maxBanking, faded in by complexity.
  for (let i = 0; i < N; i++) {
    const pm = (i - 1 + N) % N, pp = (i + 1) % N;
    const inx = xs[i] - xs[pm], inz = zs[i] - zs[pm];
    const outx = xs[pp] - xs[i], outz = zs[pp] - zs[i];
    const inl = Math.hypot(inx, inz) || 1, outl = Math.hypot(outx, outz) || 1;
    const m = Math.max(-1, Math.min(1, (inz * outx - inx * outz) / (inl * outl)));
    const turn = Math.asin(m);   // radians, + = right-hand turn
    const roll = Math.max(-ranges.maxBanking, Math.min(ranges.maxBanking, (turn / 0.6) * ranges.maxBanking)) * t;
    points.push({ type: 'roll', t: i / N, roll });
  }
  // Width varies smoothly (the width spline interpolates), blended from a
  // constant default toward the full range by complexity.
  for (let i = 0; i < N; i++) {
    const sample = ranges.widthMin + (ranges.widthMax - ranges.widthMin) * rnd();
    points.push({ type: 'width', t: i / N, width: Math.max(1, width0 + (sample - width0) * t) });
  }
  // Curvature is dish-only: sampled in [-maxCurvature*t, 0], so at full
  // complexity with the default 0.5 max it stays within [-0.5, 0] (the road
  // only ever dishes inward, never crowns).
  for (let i = 0; i < N; i++) {
    const curvature = -rnd() * ranges.maxCurvature * t;
    points.push({ type: 'crossSection', t: i / N, curvature, tightness: 1, thickness: TrackCore.DEFAULT_CROSS_SECTION_THICKNESS });
  }
  return {
    version: TrackCore.TRACK_SCHEMA_VERSION, name: 'Random Track',
    start: { path: 0, point: 0, reverse: false },
    disjointSeams: [], junctions: [], selfIntersectionOverrides: [],
    meshAssets: {}, meshes: [], textureAssets: {},
    paths: [{ id: null, closed: true, points }]
  };
}

const randomSeedEl = document.getElementById('randomSeed');
const randomComplexityEl = document.getElementById('randomComplexity');
const randomComplexityValEl = document.getElementById('randomComplexityVal');
const RANDOM_SEED_MAX = 999999;
const newRandomSeed = () => Math.floor(Math.random() * (RANDOM_SEED_MAX + 1));
randomSeedEl.value = newRandomSeed();
randomComplexityEl.addEventListener('input', () => { randomComplexityValEl.textContent = randomComplexityEl.value; });

function applyRandomTrack(seed) {
  pushUndo();
  const complexity = Number(randomComplexityEl.value) || 5;
  track = generateRandomTrack(complexity, seed, randomRanges);
  ensureTrackIds();
  assertNoStaleSeams();
  selectedPointId = null; syncSelectionToId(); segSel = null; joinSel = []; rollSel = null; widthSel = null; crossSectionSel = null; topPan = { x: 0, y: 0 };
  clearMeshSelection();
  invalidateMeshCache();
  refresh();
}
document.getElementById('randomBtn').addEventListener('click', () => {
  const seed = newRandomSeed();
  randomSeedEl.value = seed;
  applyRandomTrack(seed);
});
randomSeedEl.addEventListener('change', () => {
  const seed = Math.round(clampNum(randomSeedEl.value, 0, RANDOM_SEED_MAX, newRandomSeed()));
  randomSeedEl.value = seed;
  applyRandomTrack(seed);
});

// Ranges popup (#randomPanel): edit the bounds complexity scales within.
const RR_FIELDS = {
  lengthMin: 'rrLengthMin', lengthMax: 'rrLengthMax', turnsMin: 'rrTurnsMin', turnsMax: 'rrTurnsMax',
  maxBanking: 'rrMaxBanking', maxHill: 'rrMaxHill', widthMin: 'rrWidthMin', widthMax: 'rrWidthMax', maxCurvature: 'rrMaxCurvature'
};
function fillRandomRangeFields() {
  for (const k in RR_FIELDS) { const el = document.getElementById(RR_FIELDS[k]); if (el) el.value = randomRanges[k]; }
}
function readRandomRangeFields() {
  const raw = {};
  for (const k in RR_FIELDS) { const el = document.getElementById(RR_FIELDS[k]); raw[k] = el ? el.value : randomRanges[k]; }
  randomRanges = sanitizeRandomRanges(raw);
  saveRandomRanges();
}
const randomPanelEl = document.getElementById('randomPanel');
document.getElementById('randomRangesBtn').addEventListener('click', () => { fillRandomRangeFields(); randomPanelEl.style.display = 'block'; });
document.getElementById('closeRandomPanelBtn').addEventListener('click', () => { readRandomRangeFields(); fillRandomRangeFields(); randomPanelEl.style.display = 'none'; });
document.getElementById('randomRangesResetBtn').addEventListener('click', () => { randomRanges = { ...RANDOM_RANGE_DEFAULTS }; saveRandomRanges(); fillRandomRangeFields(); });
for (const k in RR_FIELDS) {
  const el = document.getElementById(RR_FIELDS[k]);
  if (el) el.addEventListener('change', readRandomRangeFields);
}

// ---------- Ship handling popup (#handlingPanel) ----------
// Per-track: edits write into track.handling and ride in the track JSON (not
// localStorage). normalizeHandling clamps/fills, so bad input self-corrects.
const HANDLING_FIELDS = { maxSpeed: 'hMaxSpeed', accel: 'hAccel', turnSpeed: 'hTurnSpeed', weight: 'hWeight' };
function fillHandlingFields() {
  const h = TrackCore.normalizeHandling(track.handling);
  for (const k in HANDLING_FIELDS) { const el = document.getElementById(HANDLING_FIELDS[k]); if (el) el.value = h[k]; }
}
const handlingPanelEl = document.getElementById('handlingPanel');
document.getElementById('handlingBtn').addEventListener('click', () => { fillHandlingFields(); handlingPanelEl.style.display = 'block'; });
document.getElementById('closeHandlingPanelBtn').addEventListener('click', () => { handlingPanelEl.style.display = 'none'; });
document.getElementById('handlingResetBtn').addEventListener('click', () => {
  pushUndo();
  track.handling = { ...TrackCore.DEFAULT_HANDLING };
  fillHandlingFields();
  refresh();
});
for (const k in HANDLING_FIELDS) {
  const el = document.getElementById(HANDLING_FIELDS[k]);
  if (!el) continue;
  el.addEventListener('change', () => {
    pushUndo();
    track.handling = TrackCore.normalizeHandling({ ...track.handling, [k]: el.value });
    fillHandlingFields();   // reflect any clamping back into the field
    refresh();
  });
}
document.getElementById('exportBtn').addEventListener('click', () => {
  assertNoStaleSeams();
  const json = TrackCore.serializeTrack(track);
  const blob = new Blob([json], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = (track.name || 'track').replace(/[^\w.-]+/g, '_') + '.json';
  a.click();
  URL.revokeObjectURL(a.href);
});

document.getElementById('exportUsdBtn').addEventListener('click', () => {
  assertNoStaleSeams();
  try {
    const usda = exportTrackToUSDA(track, { TrackCore });
    const blob = new Blob([usda], { type: 'model/vnd.usda' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = sanitizeFileStem(track.name, 'track') + '.usda';
    a.click();
    URL.revokeObjectURL(a.href);
  } catch (err) {
    alert('Could not export USD: ' + (err.message || err));
  }
});
// A plain <a href="track.html"> would only ever show whatever the LAST edit
// happened to persist -- which, on a fresh page load with no edits yet (a new
// track, or a track just imported before any further change), is nothing at
// all, or a stale track left over from a previous session. Persisting right
// before navigation guarantees the game tab always opens the track exactly as
// it stands right now, not "as of the last edit that happened to run refresh()".
document.getElementById('openGameLink').addEventListener('click', () => { persistEditorTrack(); });
function setPhysicsPointsVisible(visible) {
  showPhysicsPoints = visible;
  if (!visible) physicsSel = null;
  document.getElementById('showPhysicsBtn').disabled = visible;
  document.getElementById('hidePhysicsBtn').disabled = !visible;
  refresh();
}
document.getElementById('showPhysicsBtn').addEventListener('click', () => setPhysicsPointsVisible(true));
document.getElementById('hidePhysicsBtn').addEventListener('click', () => setPhysicsPointsVisible(false));
document.getElementById('joinBtn').addEventListener('click', performJoin);
document.getElementById('deleteCurveBtn').addEventListener('click', deleteSelectedCurve);
document.getElementById('undoBtn').addEventListener('click', undo);
document.getElementById('redoBtn').addEventListener('click', redo);
document.getElementById('editModeSelect').addEventListener('change', (e) => {
  setEditMode(e.target.value);
});
document.getElementById('curveSelect').addEventListener('change', (e) => {
  const pi = Number(e.target.value);
  const p = track.paths[pi] && parts(track.paths[pi]).controlPoints[0];
  if (p) selectedPointId = p.id;
  syncSelectionToId();
  rollSel = null; widthSel = null; crossSectionSel = null; segSel = null;
  refresh();
});
document.getElementById('renderModeSelect').addEventListener('change', refresh);
document.getElementById('gridSizeSelect').addEventListener('change', refresh);
document.getElementById('snapGridChk').addEventListener('change', refresh);
const topZoomSlider = document.getElementById('topZoomSlider');
let gestureStartZoomValue = 0;
function setTopZoomSliderValue(value) {
  topZoomSlider.value = Math.max(-100, Math.min(100, value));
  topZoom = Math.pow(2, Number(topZoomSlider.value) / 50); // -100..100 => 0.25x..4x
}
function zoomTopAt(x, y, zoomValue) {
  hideAddPointMenu();
  const before = screenToWorld(x, y);
  setTopZoomSliderValue(zoomValue);
  computeView(view.w, view.h);
  const after = worldToScreen(before.x, before.z);
  topPan.x += x - after.x;
  topPan.y += y - after.y;
  draw();
}
topZoomSlider.addEventListener('input', (e) => {
  setTopZoomSliderValue(Number(e.target.value));
  draw();
});
// Home: reset the top-down view to its default framing -- zoom back to 1x and
// clear the pan offset, which re-centres on the auto-fit track bounds.
document.getElementById('topHomeBtn').addEventListener('click', () => {
  hideAddPointMenu();
  setTopZoomSliderValue(0);
  topPan = { x: 0, y: 0 };
  draw();
});
function updatePointFilters() {
  refresh();
}
for (const id of ['showPositionChk', 'showRollChk', 'showWidthChk', 'showCrossSectionChk']) {
  document.getElementById(id).addEventListener('change', updatePointFilters);
}
function pointInTopCanvas(e) {
  const r = topCanvas.getBoundingClientRect();
  return e.clientX >= r.left && e.clientX <= r.right && e.clientY >= r.top && e.clientY <= r.bottom;
}
window.addEventListener('wheel', (e) => {
  if (!pointInTopCanvas(e)) return;
  // Capture + preventDefault stops browser/page zoom from Ctrl+wheel trackpad
  // pinches, then uses the gesture exclusively for the editor's top-down zoom.
  e.preventDefault();
  const { x, y } = localPos(topCanvas, e);
  zoomTopAt(x, y, Number(topZoomSlider.value) - e.deltaY * 0.16);
}, { passive: false, capture: true });
window.addEventListener('gesturestart', (e) => {
  if (!pointInTopCanvas(e)) return;
  e.preventDefault();
  gestureStartZoomValue = Number(topZoomSlider.value);
}, { passive: false, capture: true });
window.addEventListener('gesturechange', (e) => {
  if (!pointInTopCanvas(e)) return;
  e.preventDefault();
  const { x, y } = localPos(topCanvas, e);
  zoomTopAt(x, y, gestureStartZoomValue + Math.log2(e.scale) * 100);
}, { passive: false, capture: true });
document.getElementById('dirBtn').addEventListener('click', () => {
  clampStart();
  pushUndo();
  track.start.reverse = !track.start.reverse;
  refresh();
});
const fileInput = document.getElementById('fileInput');
document.getElementById('importBtn').addEventListener('click', () => fileInput.click());
fileInput.addEventListener('change', async (e) => {
  const file = e.target.files[0];
  if (!file) return;
  try {
    const parsed = TrackCore.parseTrack(await file.text());
    pushUndo(); // only after a successful parse -- a bad file shouldn't record a no-op undo step
    track = parsed;
    ensureTrackIds();
    assertNoStaleSeams();
    selectedPointId = null; syncSelectionToId(); segSel = null; joinSel = []; rollSel = null; widthSel = null; crossSectionSel = null; topPan = { x: 0, y: 0 };
    clearMeshSelection();
    invalidateMeshCache();
    refresh();
  } catch (err) { alert('Could not load track: ' + err.message); }
  e.target.value = '';
});

const meshFileInput = document.getElementById('meshFileInput');
document.getElementById('importMeshBtn').addEventListener('click', () => meshFileInput.click());
meshFileInput.addEventListener('change', async (e) => {
  const file = e.target.files[0];
  if (!file) return;
  importMeshFile(file.name, await file.text());
  e.target.value = '';
});
// Arrow, not a bare reference: the handler would otherwise receive the click
// MouseEvent as `centreOn` and try to centre the region on event.x/event.z.
document.getElementById('pasteMeshBtn').addEventListener('click', () => importMeshFromClipboard());

const textureFileInput = document.getElementById('textureFileInput');
document.getElementById('texturesBtn').addEventListener('click', () => { texturePanelOpen = true; renderTexturePanel(); });
document.getElementById('closeTexturePanelBtn').addEventListener('click', () => { texturePanelOpen = false; renderTexturePanel(); });
document.getElementById('loadTextureBtn').addEventListener('click', () => textureFileInput.click());
document.getElementById('clearCurveTextureBtn').addEventListener('click', clearCurrentCurveTexture);
textureFileInput.addEventListener('change', async (e) => {
  const file = e.target.files[0];
  if (file) await loadTextureImageFile(file);
  e.target.value = '';
});
document.getElementById('textureAssetList').addEventListener('click', (e) => {
  const tile = e.target.closest('.tile');
  if (tile) {
    assignCurrentCurveTexture(tile.dataset.asset, Number(tile.dataset.tile));
    return;
  }
  const del = e.target.closest('[data-action="deleteTexture"]');
  if (del) deleteTextureAsset(del.dataset.asset);
});
document.getElementById('textureAssetList').addEventListener('input', (e) => {
  if (e.target.dataset.action !== 'tileSize') return;
  const asset = textureAssets()[e.target.dataset.asset];
  if (!asset) return;
  const armedKey = `${e.target.dataset.asset}:${e.target.dataset.key}`;
  if (textureTileEditArmed !== armedKey) { pushUndo(); textureTileEditArmed = armedKey; updateUndoRedoButtons(); }
  asset[e.target.dataset.key] = clampTextureTileSize(asset, e.target.dataset.key, e.target.value);
  clearInvalidTextureAssignments(e.target.dataset.asset);
  persistEditorTrack();
});
document.getElementById('textureAssetList').addEventListener('change', (e) => {
  if (e.target.dataset.action === 'tileSize') renderTexturePanel();
});
document.getElementById('textureAssetList').addEventListener('blur', (e) => {
  if (e.target.dataset.action === 'tileSize') { textureTileEditArmed = null; renderTexturePanel(); }
}, true);

// Modules export nothing to the page, so expose a small read-only handle for
// debugging from the console and for browser smoke tests.
window.__editor = {
  get track() { return track; },
  get selectedMeshId() { return selectedMeshId; },
  worldToScreen,
  screenToWorld,
  compiledMeshes,
  assetMesh,
  importMeshFromClipboard,
  railCount: (assetId) => {
    const mesh = assetMesh(assetId);
    return mesh ? [...mesh.edges.values()].filter(e => e.attributes?.rail).length : 0;
  }
};

// ---------- Elevation panel collapse ----------
// Collapsing swaps one class on #app; the grid's --elev-h shrinks to a title
// bar and the top-down row (1fr) absorbs the rest. The state is remembered
// separately from the track, under its own key, so reopening the editor doesn't
// silently re-expand a panel the user put away.
const elevToggleBtn = document.getElementById('elevToggle');
function applyElevCollapsed() {
  document.getElementById('app').classList.toggle('elevCollapsed', elevCollapsed);
  elevToggleBtn.innerHTML = elevCollapsed ? '&#9652;' : '&#9662;';
  elevToggleBtn.title = elevCollapsed ? 'Expand the elevation panel' : 'Collapse the elevation panel';
  elevToggleBtn.setAttribute('aria-expanded', String(!elevCollapsed));
}
function setElevCollapsed(collapsed) {
  elevCollapsed = collapsed;
  try { localStorage.setItem(ELEV_COLLAPSED_KEY, collapsed ? '1' : '0'); } catch { /* private mode */ }
  applyElevCollapsed();
  draw();   // both canvases re-fit: the rows just changed height
}
elevToggleBtn.addEventListener('click', () => setElevCollapsed(!elevCollapsed));
applyElevCollapsed();

window.addEventListener('resize', draw);
refresh();
