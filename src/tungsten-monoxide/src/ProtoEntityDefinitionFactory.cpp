#include "ProtoEntityDefinitionFactory.h"
#include "GameException.h"

using namespace std;


ProtoEntityDefinitionFactory::ProtoEntityDefinitionFactory()
	: applib::ProtoEntityDefaultDefinitionFactory()
{
}

uint32_t ProtoEntityDefinitionFactory::getAnimationIdFromName(string const& actor, string const& anim)
{
	VAR_UNUSED(actor);
	VAR_UNUSED(anim);

	return 0;
}