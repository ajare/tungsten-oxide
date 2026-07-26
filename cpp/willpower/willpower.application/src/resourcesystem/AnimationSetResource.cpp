#include "willpower/common/StringUtils.h"
#include "willpower/common/XmlReader.h"
#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/AnimationSetResource.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			AnimationSetResource::AnimationSetResource(string const& name, string const& namesp, string const& source, map<string, string> const& tags, ResourceLocation* location)
				: Resource(name, namesp, "AnimationSet", source, tags, location)
			{
			}

			ResourcePtr AnimationSetResource::getImage()
			{
				auto imageSetRes = getDependentResource("Image");
				return static_cast<ImageSetResource*>(imageSetRes.get())->getImage();
			}

			void AnimationSetResource::destroy()
			{
				mAnimations.clear();
			}

			bool AnimationSetResource::hasAnimation(string const& name) const
			{
				return mAnimations.find(name) != mAnimations.end();
			}

			AnimationSetResource::Animation const& AnimationSetResource::getAnimation(string const& name) const
			{
				auto it = mAnimations.find(name);
				if (it == mAnimations.end())
				{
					throw ResourceException(this, "animation with the name '" + name + "' not found.");
				}

				return it->second;
			}

			vector<string> AnimationSetResource::getAnimationNames() const
			{
				vector<string> names;

				for (auto const& anim : mAnimations)
				{
					names.push_back(anim.first);
				}

				return names;
			}

			void AnimationSetResource::getMaximumDimensions(uint32_t& width, uint32_t& height) const
			{
				uint32_t maxWidth = 0, maxHeight = 0;
				for (auto item : mAnimations)
				{
					auto const&[name, anim] = item;

					for (auto const& frame : anim.frames)
					{
						maxWidth = max(maxWidth, frame.width);
						maxHeight = max(maxHeight, frame.height);
					}
				}

				width = maxWidth;
				height = maxHeight;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE