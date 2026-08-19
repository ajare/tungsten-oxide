// OrbitCamera.hpp — a new mpp::Camera subclass for model-tool's preview viewport. Nothing like an
// orbit/arcball camera exists anywhere in this codebase (mpp-helper only vendors FpsCamera/
// FreeCamera/OrthoCamera, all WASD-fly-through style) -- see docs/adr/0001-model-tool.md, D8.
//
// Deliberately does not use Camera's own incremental mYaw/mPitch/mRoll rotation mechanism (the
// same one ReactiveCamera::setOrientation, src/tungsten-monoxide, bypasses for the same reason):
// mYaw/mPitch reset to 0 after every Camera::updateAngles() call, so they can only express "rotate
// further from wherever mDirection/mUp already are", never "look from this azimuth/elevation
// around this target" as an absolute quantity. OrbitCamera instead keeps its own explicit
// spherical state (target, distance, azimuth, elevation) and recomputes mPosition/mDirection/mUp
// from scratch on every change.
#pragma once

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec3.hpp>
#pragma warning(pop)

#include <mpp/Camera.h>

namespace modeltool {

class OrbitCamera : public mpp::Camera {
  glm::vec3 mTarget{0.0f, 0.0f, 0.0f};
  float mDistance{10.0f};
  // Azimuth: degrees about world-up, 0 = looking from +Z toward the target. Elevation: degrees
  // above the target's horizontal plane, clamped away from +-90 to avoid a degenerate up vector.
  float mAzimuthDeg{35.0f};
  float mElevationDeg{25.0f};

  void recomputePose();

 public:
  explicit OrbitCamera(float fov = 45.0f, float aspectRatio = 1.0f);

  // Drag-to-orbit: adds to azimuth/elevation (elevation clamped to [-89, 89]) and recomputes pose.
  void orbit(float deltaAzimuthDeg, float deltaElevationDeg);

  // Scroll-to-zoom: multiplies distance by 0.9^wheelTicks (clamped to a small positive minimum)
  // and recomputes pose -- exponential rather than a fixed linear step, so one scroll tick feels
  // proportionally similar whether the camera is framing a 1-unit or a 1000-unit model.
  void zoom(float wheelTicks);

  // Auto-frames the camera on a model's bounding sphere: centers the target on it and sets
  // distance so the sphere comfortably fits the vertical field of view.
  void frameOnBounds(const glm::vec3& center, float radius);

  // Camera::mAspectRatio has no public setter on the base class (only ever set once, at
  // construction) -- needed here since the viewport panel this camera serves is resizable, unlike
  // every other mpp::Camera user in this codebase, which renders to a fixed-size window/target.
  void setAspectRatio(float aspectRatio);
};

}  // namespace modeltool
