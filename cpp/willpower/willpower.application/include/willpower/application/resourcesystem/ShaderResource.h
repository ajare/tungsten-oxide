#pragma once


#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API ShaderResource : public Resource
			{
				friend class ShaderResourceDefinitionFactory;

			private:

				std::string mText;

			private:

				void parseData(DataStreamPtr dataPtr) override;

				void destroy() override;

			public:

				ShaderResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, application::resourcesystem::ResourceLocation* location);

				std::string const& getText() const;
			};

			class ShaderResourceFactory : public ResourceFactory
			{
			public:

				ShaderResourceFactory()
					: ResourceFactory("Shader")
				{
				}

				Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, ResourceLocation* location) override
				{
					return new ShaderResource(name, namesp, source, tags, location);
				}
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

