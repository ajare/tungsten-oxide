#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtx/rotate_vector.hpp>
#pragma warning(pop)

#include "ReactiveCamera.h"


ReactiveCamera::ReactiveCamera(glm::vec3 const& position, float yaw, float pitch, float fov, float aspectRatio)
	: mpp::helper::FpsCamera(position, 180 - yaw, pitch, fov, aspectRatio)
{
}

void ReactiveCamera::setPosition(glm::vec3 const& position)
{
	mPosition = position;
}

void ReactiveCamera::setYaw(float yaw)
{
	mYaw = yaw;
	mDirty = true;
}

void ReactiveCamera::setPitch(float pitch)
{
	mPitch = pitch;
	mDirty = true;
}