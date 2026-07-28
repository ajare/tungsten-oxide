#include "willpower/application/resourcesystem/MaterialResource.h"

#include "TrackMaterial.h"

namespace applib
{
	using namespace std;
	using namespace wp;

	TrackMaterial::TrackMaterial(string const& name, string const& namesp, string const& source, map<string, string> const& tags, application::resourcesystem::ResourceLocation* location)
		: Resource(name, namesp, "TrackMaterial", source, tags, location)
	{
	}

	application::resourcesystem::MaterialResource* TrackMaterial::getMaterial()
	{
		return static_cast<application::resourcesystem::MaterialResource*>(getDependentResource("Material").get());
	}

} // applib
