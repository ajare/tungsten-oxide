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

			class WP_APPLICATION_API TextFileResource : public Resource
			{
				std::string mText;

			private:

				void parseData(DataStreamPtr dataPtr) override;

				void destroy() override;

			public:

				TextFileResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, application::resourcesystem::ResourceLocation* location);
			
				std::string const& getText() const;
			};

			class TextFileResourceFactory : public ResourceFactory
			{
			public:

				TextFileResourceFactory()
					: ResourceFactory("TextFile")
				{
				}

				Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, ResourceLocation* location) override
				{
					return new TextFileResource(name, namesp, source, tags, location);
				}
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

