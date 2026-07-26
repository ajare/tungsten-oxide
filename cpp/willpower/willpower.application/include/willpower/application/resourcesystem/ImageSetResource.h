#pragma once

#include "willpower/common/XmlReader.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ImageResource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API ImageSetResource : public Resource
			{
				friend class ImageSetResourceDefinitionFactory;

			public:

				struct ImageDefinition
				{
					uint32_t x, y, width, height;
					float u[2], v[2];
				};

			private:

				std::map<std::string, ImageDefinition> mImageDefinitions;

				std::map<std::string, std::vector<ImageDefinition>> mImageDefinitionSets;

			private:

				void destroy();

			public:

				ImageSetResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, application::resourcesystem::ResourceLocation* location);

				ResourcePtr getImage();

				bool hasImageDefinition(std::string const& name) const;

				bool hasImageDefinitionSet(std::string const& name) const;

				ImageDefinition const& getImageDefinition(std::string const& name) const;

				std::map<std::string, ImageDefinition> const& getImageDefinitions() const;

				std::vector<ImageDefinition> const& getImagesetDefinition(std::string const& name) const;

				std::map<std::string, std::vector<ImageDefinition>> const& getImageDefinitionSets() const;

				void getMaximumDimensions(uint32_t& width, uint32_t& height) const;
			};

			class ImageSetResourceFactory : public ResourceFactory
			{
			public:

				ImageSetResourceFactory()
					: ResourceFactory("ImageSet")
				{
				}

				Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, ResourceLocation* location) override
				{
					return new ImageSetResource(name, namesp, source, tags, location);
				}
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

#pragma once
