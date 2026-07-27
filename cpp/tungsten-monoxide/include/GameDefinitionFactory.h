#pragma once

#include <applib/GameDefaultDefinitionFactory.h>

#include "Platform.h"


class GameDefinitionFactory : public applib::GameDefaultDefinitionFactory
{
public:

	GameDefinitionFactory();

	void create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, utils::XmlNode* node) override;

};

