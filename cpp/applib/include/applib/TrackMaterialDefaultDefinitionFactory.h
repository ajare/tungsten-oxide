#pragma once

#include "Platform.h"
#include "TrackMaterialResourceDefinitionFactory.h"

namespace applib
{
	class APPLIB_API TrackMaterialDefaultDefinitionFactory : public TrackMaterialResourceDefinitionFactory
	{
	public:

		TrackMaterialDefaultDefinitionFactory();

		void create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::XmlNode* node) override;
	};

} // applib
