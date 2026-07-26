#pragma once

#include <mpp/mesh/MeshSpecification.h>


#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API ProgramResource : public Resource
			{
				friend class ProgramResourceDefinitionFactory;

			private:

				mpp::mesh::MeshSpecification mSpecification;

				uint32_t mNumTextures;

				bool mUseDiffuse, mUseColours, mUseAtlas, mUseRotation;

			private:

				bool load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

				bool unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

			public:

				ProgramResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, application::resourcesystem::ResourceLocation* location);
			};

			class ProgramResourceFactory : public ResourceFactory
			{
			public:

				ProgramResourceFactory()
					: ResourceFactory("Program")
				{
				}

				Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, ResourceLocation* location) override
				{
					return new ProgramResource(name, namesp, source, tags, location);
				}
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

