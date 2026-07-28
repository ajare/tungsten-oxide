#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/common/Exceptions.h"

#include "TrackMaterial.h"
#include "TrackMaterialDefaultDefinitionFactory.h"

namespace applib
{
	using namespace std;
	using namespace wp;

	TrackMaterialDefaultDefinitionFactory::TrackMaterialDefaultDefinitionFactory()
		: TrackMaterialResourceDefinitionFactory("")
	{
	}

	void TrackMaterialDefaultDefinitionFactory::create(application::resourcesystem::Resource* resource, application::resourcesystem::ResourceManager* resourceMgr, XmlNode* node)
	{
	}

} // applib
