#pragma once

#include <mpp/helper/FpsCamera.h>


class ReactiveCamera : public mpp::helper::FpsCamera
{
public:

	ReactiveCamera(glm::vec3 const& position, float yaw, float pitch, float fov, float aspectRatio);

	void setPosition(glm::vec3 const& position);

	void setYaw(float yaw);

	void setPitch(float pitch);
};
