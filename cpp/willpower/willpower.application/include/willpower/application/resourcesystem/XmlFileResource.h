#pragma once

#include "willpower/common/XmlReader.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API XmlFileResource : public Resource
			{
				XmlReader* mReader;

			private:

				void parseData(DataStreamPtr dataPtr) override;

				void destroy() override;

			public:

				XmlFileResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, application::resourcesystem::ResourceLocation* location);

				XmlReader* getReader();
			};

			class XmlFileResourceFactory : public ResourceFactory
			{
			public:

				XmlFileResourceFactory()
					: ResourceFactory("XmlFile")
				{
				}

				Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, ResourceLocation* location) override
				{
					return new XmlFileResource(name, namesp, source, tags, location);
				}
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

