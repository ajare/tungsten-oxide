#pragma once

#include <string>
#include <map>

#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/MaterialResource.h"

#include "Platform.h"

namespace applib
{

	class APPLIB_API TrackMaterial : public wp::application::resourcesystem::Resource
	{
	public:

		TrackMaterial(std::string const& name,
			std::string const& namesp,
			std::string const& source,
			std::map<std::string, std::string> const& tags,
			wp::application::resourcesystem::ResourceLocation* location);

		wp::application::resourcesystem::MaterialResource* getMaterial();
	};

	class APPLIB_API TrackMaterialResourceFactory : public wp::application::resourcesystem::ResourceFactory
	{
	public:

		TrackMaterialResourceFactory()
			: wp::application::resourcesystem::ResourceFactory("TrackMaterial")
		{
		}

		wp::application::resourcesystem::Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, wp::application::resourcesystem::ResourceLocation* location) override
		{
			return new TrackMaterial(name, namesp, source, tags, location);
		}
	};

} // applib
