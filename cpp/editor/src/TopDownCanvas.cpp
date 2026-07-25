#include "TopDownCanvas.hpp"

#include <cmath>
#include <vector>

#include "imgui.h"

namespace editor {
namespace {

constexpr double kWorldGridSize = 100.0;  // metres between grid lines
constexpr float kPointRadius = 4.0f;
const ImU32 kBackgroundColor = IM_COL32(8, 20, 29, 255);   // matches editor.html's #canvasWrap
const ImU32 kGridColor = IM_COL32(255, 255, 255, 18);
const ImU32 kRoadColor = IM_COL32(60, 70, 82, 255);
const ImU32 kCenterlineColor = IM_COL32(120, 170, 220, 200);
const ImU32 kPositionPointColor = IM_COL32(240, 200, 60, 255);

TrackBounds2D computeAuthoredBounds(const TrackDefinition& track) {
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

void drawAuthoredPositionPoints(ImDrawList* drawList, const ImVec2& canvasOrigin, const TopDownView& view, const TrackDefinition& track) {
  for (const auto& path : track.paths) {
    for (const auto& point : path.points) {
      if (point.kind != PointKind::Position) continue;
      const ImVec2 screen = toAbsolute(canvasOrigin, view.worldToScreen(point.pos.x, point.pos.z));
      drawList->AddCircleFilled(screen, kPointRadius, kPositionPointColor);
    }
  }
}

}  // namespace

void DrawTopDownCanvas(TopDownView& view, const TrackDefinition& authored, const tox::Track* baked) {
  const TrackBounds2D bounds = computeAuthoredBounds(authored);

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
  if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
    const ImVec2 mouseLocal = ImVec2(ImGui::GetIO().MousePos.x - canvasOrigin.x, ImGui::GetIO().MousePos.y - canvasOrigin.y);
    // 15 slider units per wheel notch (editor.js uses deltaY*0.16 against ~100px/notch deltaY,
    // i.e. ~16 units/notch); ImGui's MouseWheel is already normalized to ~1 per notch.
    view.zoomAt(mouseLocal.x, mouseLocal.y, ImGui::GetIO().MouseWheel * 15.0, bounds);
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    view.pan(delta.x, delta.y);
  }

  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(canvasOrigin, ImVec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y), kBackgroundColor);
  drawGrid(drawList, canvasOrigin, canvasSize, view);
  if (baked != nullptr)
    for (const auto& path : baked->paths) drawBakedPath(drawList, canvasOrigin, view, path);
  drawAuthoredPositionPoints(drawList, canvasOrigin, view, authored);

  ImGui::EndChild();
}

}  // namespace editor
