#include "OrbitCamera.hpp"

#include <algorithm>
#include <cmath>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#pragma warning(pop)

namespace modeltool {

OrbitCamera::OrbitCamera(float fov, float aspectRatio) : mpp::Camera(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f, fov, aspectRatio) {
  recomputePose();
}

void OrbitCamera::recomputePose() {
  const float azRad = glm::radians(mAzimuthDeg);
  const float elRad = glm::radians(mElevationDeg);
  const glm::vec3 offset(mDistance * std::cos(elRad) * std::sin(azRad), mDistance * std::sin(elRad),
                          mDistance * std::cos(elRad) * std::cos(azRad));
  mPosition = mTarget + offset;
  // Direct assignment, not Camera's own mYaw/mPitch/mRoll rotation mechanism -- see this class's
  // header comment for why an incremental-delta API can't express an absolute orbit position.
  mDirection = glm::normalize(mTarget - mPosition);
  mUp = glm::vec3(0.0f, 1.0f, 0.0f);
  mDirty = false;
}

void OrbitCamera::orbit(float deltaAzimuthDeg, float deltaElevationDeg) {
  mAzimuthDeg += deltaAzimuthDeg;
  mElevationDeg = std::clamp(mElevationDeg + deltaElevationDeg, -89.0f, 89.0f);
  recomputePose();
}

void OrbitCamera::zoom(float wheelTicks) {
  mDistance = std::max(0.01f, mDistance * std::pow(0.9f, wheelTicks));
  recomputePose();
}

void OrbitCamera::frameOnBounds(const glm::vec3& center, float radius) {
  mTarget = center;
  const float safeRadius = std::max(radius, 0.01f);
  // Distance so the bounding sphere fits comfortably inside the vertical FOV, with a little
  // breathing room (1.5x) rather than framing it edge-to-edge.
  const float halfFovRad = glm::radians(mFov * 0.5f);
  mDistance = (safeRadius * 1.5f) / std::sin(halfFovRad);
  recomputePose();
}

void OrbitCamera::setAspectRatio(float aspectRatio) { mAspectRatio = aspectRatio; }

}  // namespace modeltool
