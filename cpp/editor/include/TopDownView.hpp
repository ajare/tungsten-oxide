// TopDownView.hpp — camera/view state for the top-down 2D canvas
// (EDITOR_CPP_PORT_PLAN.md M2), mirroring js/editor.js's view/topZoom/topPan model exactly:
// an auto-fit-to-track-bounds scale times a user zoom multiplier, plus a screen-pixel pan offset
// measured from the auto-fit center. See editor.js's computeView/worldToScreen/screenToWorld/
// zoomTopAt (js/editor.js:700-745, :4533-4541).
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace editor {

struct TrackBounds2D {
  double minX{-1.0}, maxX{1.0}, minZ{-1.0}, maxZ{1.0};
};

struct WorldPoint2D {
  double x{0.0}, z{0.0};
};

struct ScreenPoint2D {
  double x{0.0}, y{0.0};
};

class TopDownView {
 public:
  // Same slider range as editor.js's TOP_ZOOM_SLIDER_MIN/MAX: 50 log2 units per doubling,
  // -100..250 => 0.25x..32x.
  static constexpr double kZoomSliderMin = -100.0;
  static constexpr double kZoomSliderMax = 250.0;
  static constexpr double kMargin = 30.0;  // px of breathing room around the auto-fit bounds

  // Recomputes scale/origin for the given viewport size and current track bounds. Call once per
  // frame before drawing or converting coordinates (view.js does this at the top of draw()).
  void computeView(const TrackBounds2D& requestedBounds, double viewportW, double viewportH) {
    const TrackBounds2D& bounds = frozenBounds_.has_value() ? *frozenBounds_ : requestedBounds;
    width_ = std::max(1.0, viewportW);
    height_ = std::max(1.0, viewportH);
    const double spanX = (bounds.maxX - bounds.minX) != 0.0 ? (bounds.maxX - bounds.minX) : 1.0;
    const double spanZ = (bounds.maxZ - bounds.minZ) != 0.0 ? (bounds.maxZ - bounds.minZ) : 1.0;
    const double baseScale = std::min((width_ - 2.0 * kMargin) / spanX, (height_ - 2.0 * kMargin) / spanZ);
    scale_ = baseScale * zoomMultiplier();
    const double cx = (bounds.minX + bounds.maxX) / 2.0, cz = (bounds.minZ + bounds.maxZ) / 2.0;
    originX_ = width_ / 2.0 - cx * scale_ + panX_;
    originY_ = height_ / 2.0 - cz * scale_ + panY_;
  }

  ScreenPoint2D worldToScreen(double x, double z) const { return {x * scale_ + originX_, z * scale_ + originY_}; }
  WorldPoint2D screenToWorld(double sx, double sy) const { return {(sx - originX_) / scale_, (sy - originY_) / scale_}; }

  void pan(double dxScreen, double dyScreen) {
    panX_ += dxScreen;
    panY_ += dyScreen;
  }

  // Adjusts the zoom slider by `deltaSliderUnits`, re-anchoring so the world point currently
  // under (screenX, screenY) stays under the cursor -- matches zoomTopAt's before/after
  // screenToWorld/worldToScreen dance. Must be called with the *current* bounds so the
  // re-anchoring math uses the same view the caller just drew with.
  void zoomAt(double screenX, double screenY, double deltaSliderUnits, const TrackBounds2D& bounds) {
    const WorldPoint2D before = screenToWorld(screenX, screenY);
    zoomSlider_ = std::clamp(zoomSlider_ + deltaSliderUnits, kZoomSliderMin, kZoomSliderMax);
    computeView(bounds, width_, height_);
    const ScreenPoint2D after = worldToScreen(before.x, before.z);
    panX_ += screenX - after.x;
    panY_ += screenY - after.y;
    computeView(bounds, width_, height_);
  }

  // Home: zoom back to 1x and clear pan, re-centering on the auto-fit bounds.
  void resetView() {
    zoomSlider_ = 0.0;
    panX_ = panY_ = 0.0;
  }

  double zoomMultiplier() const { return std::pow(2.0, zoomSlider_ / 50.0); }
  double scale() const { return scale_; }

  // Raw slider value in [kZoomSliderMin, kZoomSliderMax] (0 => 1x, 50 units per doubling) --
  // mirrors editor.js's #topZoomSlider. Unlike zoomAt(), setZoomSlider() does NOT re-anchor on a
  // screen point: editor.js's own topZoomSlider 'input' handler calls setTopZoomSliderValue()
  // directly (not zoomTopAt()), so dragging the slider zooms about the view's current center
  // (auto-fit center + pan), the same way this method does by leaving panX_/panY_ untouched.
  double zoomSlider() const { return zoomSlider_; }
  void setZoomSlider(double value) { zoomSlider_ = std::clamp(value, kZoomSliderMin, kZoomSliderMax); }

  // The world point currently at the viewport's centre pixel -- mirrors editor.js's
  // screenToWorld(view.w/2, view.h/2), used to centre a freshly imported mesh asset (M9) when the
  // caller has no click position to place it at.
  WorldPoint2D center() const { return screenToWorld(width_ / 2.0, height_ / 2.0); }

  // Mirrors editor.js's frozenViewBounds: while a point is being dragged, computeView() must keep
  // using the bounds captured *before* the drag started, not the ones the moving point produces
  // each frame -- otherwise the auto-fit view fights the drag (it re-centers/rescales around the
  // very point the user is trying to move, so the point barely appears to move on screen at all).
  void freezeBounds(const TrackBounds2D& bounds) {
    if (!frozenBounds_.has_value()) frozenBounds_ = bounds;
  }
  void releaseBoundsFreeze() { frozenBounds_.reset(); }

  // Top-down grid display / snap-to-grid (EDITOR_PARITY_FIXES.md gap 9), mirroring editor.js's
  // module-level showGrid/gridSize/snapToGrid: UI/view preferences, not track data, so they live
  // here rather than in EditorState/undo history. showGrid(false) does NOT clear snapToGrid_ --
  // the checkbox's own preference is retained so re-showing the grid restores the prior snap
  // setting (CLAUDE.md's editor conventions), and snapWorldXZ() re-checks showGrid_ itself so a
  // hidden grid can never leave snapping silently active.
  bool showGrid() const { return showGrid_; }
  void setShowGrid(bool show) { showGrid_ = show; }
  double gridSize() const { return gridSize_; }
  void setGridSize(double size) { gridSize_ = size; }
  bool snapToGrid() const { return snapToGrid_; }
  void setSnapToGrid(bool snap) { snapToGrid_ = snap; }

  // Mirrors snapWorldXZ(): a no-op unless both the grid is visible and snap is enabled.
  WorldPoint2D snapWorldXZ(const WorldPoint2D& w) const {
    if (!showGrid_ || !snapToGrid_) return w;
    return {std::round(w.x / gridSize_) * gridSize_, std::round(w.z / gridSize_) * gridSize_};
  }

  // Render mode / point-type filters / physics-sample overlay (EDITOR_PARITY_FIXES.md gap 10),
  // mirroring editor.js's module-level renderMode/pointFilters/showPhysicsPoints/physicsSel: all
  // view/UI preferences, not track data, so -- like grid/snap above -- they live here rather than
  // in EditorState/undo history.
  enum class RenderMode { Banked, Flat, Elevation };

  RenderMode renderMode() const { return renderMode_; }
  void setRenderMode(RenderMode mode) { renderMode_ = mode; }

  // Only `showPositionPoints` currently has an observable effect: roll/width/crossSection points
  // have no on-canvas presence at all yet in this editor (EDITOR_PARITY_FIXES.md gap 1 -- they're
  // panel-only), so hiding/showing them here is a no-op until that on-canvas rendering exists.
  // The fields and accessors still exist so the toolbar checkboxes match editor.html's four,
  // rather than silently dropping three of them.
  bool showPositionPoints() const { return showPositionPoints_; }
  void setShowPositionPoints(bool show) { showPositionPoints_ = show; }
  bool showRollPoints() const { return showRollPoints_; }
  void setShowRollPoints(bool show) { showRollPoints_ = show; }
  bool showWidthPoints() const { return showWidthPoints_; }
  void setShowWidthPoints(bool show) { showWidthPoints_ = show; }
  bool showCrossSectionPoints() const { return showCrossSectionPoints_; }
  void setShowCrossSectionPoints(bool show) { showCrossSectionPoints_ = show; }

  struct PhysicsSampleRef {
    int pathIndex, frameIndex;
  };

  bool showPhysicsPoints() const { return showPhysicsPoints_; }
  // Mirrors setPhysicsPointsVisible: hiding the overlay also drops any active selection, since a
  // hidden dot can't stay "selected" in any way the user can see.
  void setShowPhysicsPoints(bool show) {
    showPhysicsPoints_ = show;
    if (!show) physicsSelection_.reset();
  }
  const std::optional<PhysicsSampleRef>& physicsSelection() const { return physicsSelection_; }
  void selectPhysicsSample(int pathIndex, int frameIndex) { physicsSelection_ = PhysicsSampleRef{pathIndex, frameIndex}; }
  void clearPhysicsSelection() { physicsSelection_.reset(); }

 private:
  double scale_{1.0}, originX_{0.0}, originY_{0.0};
  double width_{1.0}, height_{1.0};
  double zoomSlider_{0.0};  // 0 => 1x, matching editor.js's initial topZoom = 1
  double panX_{0.0}, panY_{0.0};
  std::optional<TrackBounds2D> frozenBounds_;
  bool showGrid_{true};
  double gridSize_{32.0};
  bool snapToGrid_{false};
  RenderMode renderMode_{RenderMode::Banked};
  bool showPositionPoints_{true}, showRollPoints_{true}, showWidthPoints_{true}, showCrossSectionPoints_{true};
  bool showPhysicsPoints_{false};
  std::optional<PhysicsSampleRef> physicsSelection_;
};

}  // namespace editor
