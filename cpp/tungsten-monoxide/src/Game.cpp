#include "willpower/application/resourcesystem/ResourceExceptions.h"

#include "Game.h"

using namespace std;
using namespace wp;


Game::Game(string const& name,
	string const& namesp,
	string const& source,
	map<string, string> const& tags,
	application::resourcesystem::ResourceLocation* location,
	shared_ptr<applib::AnimationDatabase> animDatabase)
	: applib::Game(name, namesp, source, tags, location, animDatabase)
{
}

uint32_t Game::getBulletReferenceId(string const& bulletName)
{
	VAR_UNUSED(bulletName);
	return ~0u;
}

uint32_t Game::getBulletAnimationReferenceId(string const& animationType)
{
	VAR_UNUSED(animationType);
	return ~0u;
}