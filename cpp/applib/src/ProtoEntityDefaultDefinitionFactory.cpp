#include <utils/StringUtils.h>
#include <utils/XmlReader.h>

#include "willpower/application/resourcesystem/AnimationSetResource.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/common/Exceptions.h"

#include "ProtoEntity.h"
#include "ProtoEntityDefaultDefinitionFactory.h"

namespace applib
{
	using namespace std;
	using namespace utils;
	using namespace wp;

	ProtoEntityDefaultDefinitionFactory::ProtoEntityDefaultDefinitionFactory()
		: ProtoEntityResourceDefinitionFactory("")
	{
	}

	void ProtoEntityDefaultDefinitionFactory::createProtoEntity(ProtoEntity* entity, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::XmlNode* node)
	{
	}

} // applib
