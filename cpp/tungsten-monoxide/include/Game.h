#pragma once

#include <string>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

#include "applib/Game.h"

#include "Platform.h"


class Game : public applib::Game
{
public:

	Game(std::string const& name,
		std::string const& namesp,
		std::string const& source,
		std::map<std::string, std::string> const& tags,
		wp::application::resourcesystem::ResourceLocation* location,
		std::shared_ptr<applib::AnimationDatabase> animDatabase);

	uint32_t getBulletReferenceId(std::string const& bulletName) override;

	uint32_t getBulletAnimationReferenceId(std::string const& animationType) override;
};

class GameResourceFactory : public wp::application::resourcesystem::ResourceFactory
{
	std::shared_ptr<applib::AnimationDatabase> mAnimationDatabase;

public:

	explicit GameResourceFactory(std::shared_ptr<applib::AnimationDatabase> animDatabase)
		: wp::application::resourcesystem::ResourceFactory("Game")
		, mAnimationDatabase(animDatabase)
	{
	}

	wp::application::resourcesystem::Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, wp::application::resourcesystem::ResourceLocation* location) override
	{
		return new Game(name, namesp, source, tags, location, mAnimationDatabase);
	}
};
