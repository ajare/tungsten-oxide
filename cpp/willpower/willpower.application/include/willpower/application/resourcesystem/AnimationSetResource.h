#pragma once

#include "willpower/common/XmlReader.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/ImageSetResource.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			class AnimationSetResourceDefinitionFactory;

			class WP_APPLICATION_API AnimationSetResource : public Resource
			{
				friend class AnimationSetResourceDefinitionFactory;

			public:

				enum class LoopStyle
				{
					Forwards,
					Once,
					PingPong
				};

			public:

				struct Frame
				{
					uint32_t srcPosX, srcPosY;
					uint32_t width, height;
					uint32_t renderOffsetX, renderOffsetY;
					float u[2], v[2];
					float time;
					std::map<std::string, std::string> tags;
				};

				typedef std::vector<Frame> FrameSet;

				struct Animation
				{
					std::string name;
					LoopStyle loopStyle;
					FrameSet frames;
				};

			private:

				std::map<std::string, Animation> mAnimations;

			private:

				void destroy();

			public:

				AnimationSetResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, application::resourcesystem::ResourceLocation* location);

				ResourcePtr getImage();

				bool hasAnimation(std::string const& name) const;

				Animation const& getAnimation(std::string const& name) const;

				std::vector<std::string> getAnimationNames() const;

				void getMaximumDimensions(uint32_t& width, uint32_t& height) const;
			};

			class AnimationSetResourceFactory : public ResourceFactory
			{
			public:

				AnimationSetResourceFactory()
					: ResourceFactory("AnimationSet")
				{
				}

				Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, ResourceLocation* location) override
				{
					return new AnimationSetResource(name, namesp, source, tags, location);
				}
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE

