#pragma once

#include <mpp/helper/FpsCamera.h>


class ReactiveCamera : public mpp::helper::FpsCamera
{
public:

	ReactiveCamera(glm::vec3 const& position, float yaw, float pitch, float fov, float aspectRatio);

	void setPosition(glm::vec3 const& position);

	void setYaw(float yaw);

	void setPitch(float pitch);

	// Sets the camera's look direction and up vector directly, rather than through
	// setYaw()/setPitch(): those are relative rotations applied on top of whatever mDirection/mUp
	// already are (mYaw/mPitch/mRoll get reset to 0 by Camera::updateAngles() immediately after
	// each use), so there's no way to express an absolute world-space orientation -- e.g. a
	// starting-grid pose's forward/up vectors, where up may not be world-up on a banked track --
	// through them. mDirection/mUp are protected on mpp::Camera, so a derived class can assign
	// them directly; forward/up are normalized here since Camera::getViewTransform()'s lookAt()
	// call assumes both already are.
	void setOrientation(glm::vec3 const& forward, glm::vec3 const& up);
};
