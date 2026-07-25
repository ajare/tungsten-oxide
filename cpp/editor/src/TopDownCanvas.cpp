#include "TopDownCanvas.hpp"

#include <cmath>
#include <numbers>
#include <vector>

#include "imgui.h"

namespace editor {
namespace {

constexpr double kWorldGridSize = 100.0;  // metres between grid lines
constexpr float kPointRadius = 4.0f;
constexpr float kPickRadiusPx = 10.0f;  // matches editor.js's nodeAtTop hit radius
const ImU32 kBackgroundColor = IM_COL32(8, 20, 29, 255);   // matches editor.html's #canvasWrap
const ImU32 kGridColor = IM_COL32(255, 255, 255, 18);
const ImU32 kRoadColor = IM_COL32(60, 70, 82, 255);
const ImU32 kCenterlineColor = IM_COL32(120, 170, 220, 200);
const ImU32 kPositionPointColor = IM_COL32(240, 200, 60, 255);
const ImU32 kSelectedPointColor = IM_COL32(255, 90, 90, 255);
const ImU32 kCreateDraftColor = IM_COL32(120, 230, 140, 255);
const ImU32 kMeshFillColor = IM_COL32(90, 110, 70, 200);
const ImU32 kMeshOutlineColor = IM_COL32(150, 190, 110, 255);
const ImU32 kMeshSelectedOutlineColor = IM_COL32(255, 90, 90, 255);

// Mirrors editor.js's computeTrackBounds: authored control points plus every mesh region's baked
// bounds, so a placed mesh always fits in the auto-fit view even off to one side of the track.
TrackBounds2D computeViewBounds(const TrackDefinition& track, const tox::Track* baked) {
  TrackBounds2D bounds{1e300, -1e300, 1e300, -1e300};
  for (const auto& path : track.paths) {
    for (const auto& point : path.points) {
      if (point.kind != PointKind::Position) continue;
      bounds.minX = std::min(bounds.minX, point.pos.x);
      bounds.maxX = std::max(bounds.maxX, point.pos.x);
      bounds.minZ = std::min(bounds.minZ, point.pos.z);
      bounds.maxZ = std::max(bounds.maxZ, point.pos.z);
    }
  }
  if (baked != nullptr) {
    for (const auto& region : baked->meshRegions) {
      bounds.minX = std::min(bounds.minX, region.bounds.minX);
      bounds.maxX = std::max(bounds.maxX, region.bounds.maxX);
      bounds.minZ = std::min(bounds.minZ, region.bounds.minZ);
      bounds.maxZ = std::max(bounds.maxZ, region.bounds.maxZ);
    }
  }
  if (bounds.minX > bounds.maxX) return TrackBounds2D{-1.0, 1.0, -1.0, 1.0};
  return bounds;
}

ImVec2 toAbsolute(const ImVec2& canvasOrigin, const ScreenPoint2D& local) {
  return ImVec2(canvasOrigin.x + static_cast<float>(local.x), canvasOrigin.y + static_cast<float>(local.y));
}

void drawGrid(ImDrawList* drawList, const ImVec2& canvasOrigin, const ImVec2& canvasSize, const TopDownView& view) {
  const double step = kWorldGridSize * view.scale();
  if (step < 4.0) return;  // grid would be denser than pixels can show -- skip rather than smear
  const WorldPoint2D topLeft = view.screenToWorld(0.0, 0.0);
  const WorldPoint2D bottomRight = view.screenToWorld(canvasSize.x, canvasSize.y);
  const double startX = std::floor(topLeft.x / kWorldGridSize) * kWorldGridSize;
  for (double x = startX; x <= bottomRight.x; x += kWorldGridSize) {
    const ScreenPoint2D top = view.worldToScreen(x, topLeft.z);
    const ScreenPoint2D bottom = view.worldToScreen(x, bottomRight.z);
    drawList->AddLine(toAbsolute(canvasOrigin, top), toAbsolute(canvasOrigin, bottom), kGridColor);
  }
  const double startZ = std::floor(topLeft.z / kWorldGridSize) * kWorldGridSize;
  for (double z = startZ; z <= bottomRight.z; z += kWorldGridSize) {
    const ScreenPoint2D left = view.worldToScreen(topLeft.x, z);
    const ScreenPoint2D right = view.worldToScreen(bottomRight.x, z);
    drawList->AddLine(toAbsolute(canvasOrigin, left), toAbsolute(canvasOrigin, right), kGridColor);
  }
}

void drawBakedPath(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const tox::Path& path) {
  const std::size_t n = path.centerline.size();
  if (n < 2) return;

  // Road band: one filled quad per centerline segment, using each frame's baked edgeRight/halfW
  // (flat, ignoring cross-section banking -- a top-down view has no elevation axis anyway).
  const std::size_t segmentCount = path.closed ? n : n - 1;
  for (std::size_t i = 0; i < segmentCount; ++i) {
    const std::size_t j = (i + 1) % n;
    const tox::Frame& fi = path.centerline[i];
    const tox::Frame& fj = path.centerline[j];
    const tox::Vec3 leftI = fi.pos.clone().addScaledVector(fi.edgeRight, -fi.halfW);
    const tox::Vec3 rightI = fi.pos.clone().addScaledVector(fi.edgeRight, fi.halfW);
    const tox::Vec3 leftJ = fj.pos.clone().addScaledVector(fj.edgeRight, -fj.halfW);
    const tox::Vec3 rightJ = fj.pos.clone().addScaledVector(fj.edgeRight, fj.halfW);
    const ImVec2 quad[4] = {
        toAbsolute(canvasOrigin, view.worldToScreen(leftI.x, leftI.z)),
        toAbsolute(canvasOrigin, view.worldToScreen(leftJ.x, leftJ.z)),
        toAbsolute(canvasOrigin, view.worldToScreen(rightJ.x, rightJ.z)),
        toAbsolute(canvasOrigin, view.worldToScreen(rightI.x, rightI.z)),
    };
    drawList->AddConvexPolyFilled(quad, 4, kRoadColor);
  }

  std::vector<ImVec2> centerline;
  centerline.reserve(n);
  for (const auto& frame : path.centerline) centerline.push_back(toAbsolute(canvasOrigin, view.worldToScreen(frame.pos.x, frame.pos.z)));
  drawList->AddPolyline(centerline.data(), static_cast<int>(centerline.size()), kCenterlineColor,
                        path.closed ? ImDrawFlags_Closed : ImDrawFlags_None, 2.0f);
}

// Mesh polygons are already baked into world space by core's compileTrackMeshes (Vec2d{x, y}
// where y is world Z, matching js/track-mesh.js's localToWorld) -- the editor draws that directly
// rather than re-deriving placement transforms itself, the same reuse-core approach as the road.
void drawMeshRegions(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const std::vector<tox::MeshRegion>& regions,
                     const std::optional<std::string>& selectedMeshId) {
  for (const auto& region : regions) {
    const bool isSelected = selectedMeshId.has_value() && *selectedMeshId == region.id;
    for (const auto& polygon : region.polygons) {
      if (polygon.outer.size() < 3) continue;
      std::vector<ImVec2> screen;
      screen.reserve(polygon.outer.size());
      for (const auto& v : polygon.outer) screen.push_back(toAbsolute(canvasOrigin, view.worldToScreen(v.x, v.y)));
      drawList->AddConcavePolyFilled(screen.data(), static_cast<int>(screen.size()), kMeshFillColor);
      drawList->AddPolyline(screen.data(), static_cast<int>(screen.size()), isSelected ? kMeshSelectedOutlineColor : kMeshOutlineColor,
                            ImDrawFlags_Closed, isSelected ? 3.0f : 1.5f);
    }
  }
}

void drawAuthoredPositionPoints(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const TrackDefinition& track,
                                const SelectedPoint& selection) {
  for (int pi = 0; pi < static_cast<int>(track.paths.size()); ++pi) {
    const auto& points = track.paths[pi].points;
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
      if (points[i].kind != PointKind::Position) continue;
      const bool isSelected = selection.pathIndex == pi && selection.pointIndex == i;
      const ImVec2 screen = toAbsolute(canvasOrigin, view.worldToScreen(points[i].pos.x, points[i].pos.z));
      drawList->AddCircleFilled(screen, isSelected ? kPointRadius + 2.0f : kPointRadius, isSelected ? kSelectedPointColor : kPositionPointColor);
    }
  }
}

void drawCreateDraft(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const std::vector<tox::Vec3>& draft) {
  if (draft.empty()) return;
  std::vector<ImVec2> screen;
  screen.reserve(draft.size());
  for (const auto& p : draft) screen.push_back(toAbsolute(canvasOrigin, view.worldToScreen(p.x, p.z)));
  if (screen.size() > 1) drawList->AddPolyline(screen.data(), static_cast<int>(screen.size()), kCreateDraftColor, ImDrawFlags_None, 2.0f);
  for (const auto& s : screen) drawList->AddCircleFilled(s, kPointRadius, kCreateDraftColor);
}

// Mesh regions are hit-tested against the baked (world-space) region polygons, matched back to
// the authored placement by id -- picked only after every position point, so a large region can
// never steal a click from a control point drawn on top of it (CLAUDE.md's editor conventions).
const tox::MeshRegion* meshRegionAt(const tox::Track* baked, double worldX, double worldZ) {
  if (baked == nullptr) return nullptr;
  for (const auto& region : baked->meshRegions)
    if (region.contains(worldX, worldZ)) return &region;
  return nullptr;
}

double angleFromOriginDeg(double originX, double originZ, double worldX, double worldZ) {
  return std::atan2(worldZ - originZ, worldX - originX) * 180.0 / std::numbers::pi;
}

// Left click/drag: hit-test + select + move a position point or mesh placement (Edit mode only --
// mirrors editor.js's dragging === 'top'/'meshTop'/'meshRotate'). Shift+drag on a mesh rotates it
// about its own placement origin instead of moving it. Returns true if the track was mutated this
// frame. Freezes the view's auto-fit bounds for the duration of any drag (see
// TopDownView::freezeBounds) so moving/rotating something doesn't fight the camera auto-fitting
// around it.
bool handleEditModeInput(EditorState& state, TopDownView& view, const tox::Track* baked, const TrackBounds2D& preDragBounds,
                         const ImVec2& mouseLocal, double pickRadiusWorld, bool hovered) {
  bool mutated = false;
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
    if (!state.selectPositionAt(world.x, world.z, pickRadiusWorld)) {
      const tox::MeshRegion* region = meshRegionAt(baked, world.x, world.z);
      if (region != nullptr) state.selectMesh(region->id);
      else state.clearMeshSelection();
    }
  }

  const bool draggingGesture = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f);
  if (draggingGesture && state.selection().valid()) {
    if (!state.dragging()) {
      state.beginDrag();
      view.freezeBounds(preDragBounds);
    }
    const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
    state.dragSelectedTo(world.x, world.z);
    mutated = true;
  } else if (state.dragging() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    state.endDrag();
    view.releaseBoundsFreeze();
  } else if (draggingGesture && state.selectedMeshId().has_value()) {
    const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
    const bool rotate = ImGui::GetIO().KeyShift;
    if (!state.meshDragging() && !state.meshRotating()) {
      view.freezeBounds(preDragBounds);
      if (rotate) {
        const MeshPlacement* placement = state.findMeshPlacement(*state.selectedMeshId());
        if (placement != nullptr) state.beginMeshRotate(angleFromOriginDeg(placement->x, placement->z, world.x, world.z));
      } else {
        state.beginMeshDrag(world.x, world.z);
      }
    }
    if (state.meshRotating()) {
      const MeshPlacement* placement = state.findMeshPlacement(*state.selectedMeshId());
      if (placement != nullptr) state.dragMeshRotateTo(angleFromOriginDeg(placement->x, placement->z, world.x, world.z));
    } else if (state.meshDragging()) {
      state.dragMeshTo(world.x, world.z);
    }
    mutated = true;
  } else if ((state.meshDragging() || state.meshRotating()) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    state.endMeshDrag();
    state.endMeshRotate();
    view.releaseBoundsFreeze();
  }
  return mutated;
}

// Left click: add a draft point, or close/finish the draft (mirrors createModeClick). Right
// click: cancel the draft (mirrors create mode's button===2 handling).
bool handleCreateModeInput(EditorState& state, const TopDownView& view, const ImVec2& mouseLocal, double pickRadiusWorld, bool hovered) {
  if (!hovered) return false;
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    state.cancelCreateDraft();
    return false;
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const WorldPoint2D world = view.screenToWorld(mouseLocal.x, mouseLocal.y);
    return state.createModeClick(world.x, world.z, pickRadiusWorld);
  }
  return false;
}

}  // namespace

bool DrawTopDownCanvas(TopDownView& view, EditorState& state, const tox::Track* baked) {
  const TrackBounds2D bounds = computeViewBounds(state.track(), baked);

  if (ImGui::Button("Home")) view.resetView();

  ImGui::BeginChild("TopDownCanvas", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.x = std::max(1.0f, canvasSize.x);
  canvasSize.y = std::max(1.0f, canvasSize.y);

  view.computeView(bounds, canvasSize.x, canvasSize.y);

  ImGui::InvisibleButton("topDownCanvasInput", canvasSize,
                          ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const bool windowFocused = ImGui::IsWindowFocused();
  const ImVec2 mouseLocal = ImVec2(ImGui::GetIO().MousePos.x - canvasOrigin.x, ImGui::GetIO().MousePos.y - canvasOrigin.y);

  if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
    // 15 slider units per wheel notch (editor.js uses deltaY*0.16 against ~100px/notch deltaY,
    // i.e. ~16 units/notch); ImGui's MouseWheel is already normalized to ~1 per notch.
    view.zoomAt(mouseLocal.x, mouseLocal.y, ImGui::GetIO().MouseWheel * 15.0, bounds);
  }

  bool mutated = false;
  const double pickRadiusWorld = kPickRadiusPx / view.scale();
  switch (state.mode()) {
    case EditMode::Edit:
      mutated = handleEditModeInput(state, view, baked, bounds, mouseLocal, pickRadiusWorld, hovered);
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) view.pan(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
      if (windowFocused && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        if (state.selection().valid()) mutated = state.deleteSelectedPoint() || mutated;
        else if (state.selectedMeshId().has_value()) mutated = state.deleteSelectedMesh() || mutated;
      }
      break;
    case EditMode::Create:
      mutated = handleCreateModeInput(state, view, mouseLocal, pickRadiusWorld, hovered);
      if (mutated) state.setMode(EditMode::Edit);  // mirrors setEditMode('edit') after finishCreateDraft
      break;
    case EditMode::Rails:
      // Rail-edge flagging is M5+ -- mesh regions exist as of M4, but nothing is pickable as a
      // rail yet, so Rails mode still just pans.
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) view.pan(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
      break;
  }

  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(canvasOrigin, ImVec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y), kBackgroundColor);
  drawGrid(drawList, canvasOrigin, canvasSize, view);
  if (baked != nullptr) {
    for (const auto& path : baked->paths) drawBakedPath(drawList, canvasOrigin, view, path);
    drawMeshRegions(drawList, canvasOrigin, view, baked->meshRegions, state.selectedMeshId());
  }
  drawAuthoredPositionPoints(drawList, canvasOrigin, view, state.track(), state.selection());
  if (state.mode() == EditMode::Create) drawCreateDraft(drawList, canvasOrigin, view, state.createDraft());

  ImGui::EndChild();
  return mutated;
}

}  // namespace editor
