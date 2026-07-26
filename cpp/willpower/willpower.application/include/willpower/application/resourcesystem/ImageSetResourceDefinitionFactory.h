#pragma once

#include "willpower/common/XmlReader.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ImageResource.h"
#include "willpower/application/resourcesystem/ImageSetResource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API ImageSetResourceDefinitionFactory : public ResourceDefinitionFactory
			{
			protected:

				void clear(ImageSetResource* resource);

				void addImageDefinition(ImageSetResource* resource, std::string const& name, ImageSetResource::ImageDefinition const& def);

				void addImageDefinitionSet(ImageSetResource* resource, std::string const& name, std::vector<ImageSetResource::ImageDefinition> const& defs);

				void validateCoordinates(ImageSetResource* resource, std::string const& name, int x, int y, int width, int height);

				void calculateUvCoords(ImageSetResource* resource, ImageSetResource::ImageDefinition* imageDef);

				ImageSetResource::ImageDefinition createImageDefinition(ImageSetResource* resource, std::string const& name, XmlNode* node);

			public:

				explicit ImageSetResourceDefinitionFactory(std::string const& factoryType);
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

#pragma once
