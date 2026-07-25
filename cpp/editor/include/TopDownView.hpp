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

 private:
  double scale_{1.0}, originX_{0.0}, originY_{0.0};
  double width_{1.0}, height_{1.0};
  double zoomSlider_{0.0};  // 0 => 1x, matching editor.js's initial topZoom = 1
  double panX_{0.0}, panY_{0.0};
  std::optional<TrackBounds2D> frozenBounds_;
};

}  // namespace editor
