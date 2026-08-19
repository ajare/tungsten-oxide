#pragma once

#include <vector>

#include <applib/EntityHandler.h>

#include "Platform.h"


class EntityHandlerTungstenMonoxide : public applib::EntityHandler
{
	bool mInputEnabled;

private:

	void updateVisual(applib::Entity* entity, float frameTime);

	std::string getPrototypeName(int type) override;

	void setupImpl(applib::Entity* entity) override;

	void destroyImpl(applib::Entity* entity) override;

	bool updateImpl(applib::Entity *entity, bool inputControlled, float frameTime) override;

public:

	EntityHandlerTungstenMonoxide();

	void enableInput(bool enable);

	bool isInputEnabled() const;

	void peekInput(applib::Entity const& entity, wp::Vector2* curPosition, wp::Vector2* newPosition, float* curAngle, float* newAngle, float* curPitch, float* newPitch, wp::Vector2* velocity, float frameTime) const;

	bool update(applib::Entity* entity, bool controlActive, float frameTime) override;
};