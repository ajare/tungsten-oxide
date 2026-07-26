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

			class WP_APPLICATION_API MaterialResource : public Resource
			{
				friend class MaterialResourceDefinitionFactory;

			public:

				struct TextureDefinition
				{
					std::string samplerName;
					bool isResource;
					std::string resourceName;
				};

			private:

				std::vector<TextureDefinition> mTextureDefinitions;

			private:

				bool load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

				bool unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

				uint32_t getNumTextureDefinitions() const;

				TextureDefinition const& getTextureDefinition(uint32_t index) const;

			public:

				MaterialResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, application::resourcesystem::ResourceLocation* location);

				uint32_t getNumTextures() const;

				uint32_t getTextureWidth(uint32_t index) const;

				uint32_t getTextureHeight(uint32_t index) const;
			};

			class MaterialResourceFactory : public ResourceFactory
			{
			public:

				MaterialResourceFactory()
					: ResourceFactory("Material")
				{
				}

				Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, ResourceLocation* location) override
				{
					return new MaterialResource(name, namesp, source, tags, location);
				}
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

#pragma once
