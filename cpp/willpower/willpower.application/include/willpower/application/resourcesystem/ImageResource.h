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

			class WP_APPLICATION_API ImageResource : public Resource
			{
				uint8_t* mData;

				uint32_t mSize;

				int mWidth;

				int mHeight;

				int mNumChannels;

			private:

				void parseData(DataStreamPtr dataPtr) override;

				void destroy() override;

				bool load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

				bool unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

			public:

				ImageResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, application::resourcesystem::ResourceLocation* location);

				uint8_t const* getData() const;

				uint32_t getSize() const;

				int getWidth() const;

				int getHeight() const;

				int getNumChannels() const;
			};

			class ImageResourceFactory : public ResourceFactory
			{
			public:

				ImageResourceFactory()
					: ResourceFactory("Image")
				{
				}

				Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, ResourceLocation* location) override
				{
					return new ImageResource(name, namesp, source, tags, location);
				}
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

