#include "willpower/common/StringUtils.h"
#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/AnimationSetDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			AnimationSetDefaultDefinitionFactory::AnimationSetDefaultDefinitionFactory()
				: AnimationSetResourceDefinitionFactory("")
			{
			}

			void AnimationSetDefaultDefinitionFactory::create(Resource* resource, ResourceManager* resourceMgr, XmlNode* node)
			{
				WP_UNUSED(resourceMgr);

				auto animSetRes = static_cast<AnimationSetResource*>(resource);

				clear(animSetRes);

				auto animationsNode = node->getChild("Animations");
				auto animationNode = animationsNode->getOptionalChild("Animation");
				if (animationNode)
				{
					do
					{
						// Name
						auto animName = animationNode->getAttribute("name");
						if (animSetRes->hasAnimation(animName))
						{
							throw ResourceException(animSetRes, "animation with the name '" + animName + "' specified multiple times.");
						}

						// Loop style
						auto ls = parseLoopStyle(animSetRes, animName, animationNode);

						AnimationSetResource::Animation anim
						{
							animName,
							ls
						};

						// Read frames
						auto framesNode = animationNode->getChild("Frames");

						// Get core attributes, most optional at this level.
						string imageset;
						bool hasImageset = framesNode->getOptionalAttribute("imageset", imageset);

						string countStr;
						bool hasCount = framesNode->getOptionalAttribute("count", countStr);
						uint32_t count = hasCount ? StringUtils::parseUInt(countStr) : -1;

						string timeStr;
						bool hasTime = framesNode->getOptionalAttribute("time", timeStr);
						float time = hasTime ? StringUtils::parseFloat(timeStr) : -1.0f;
						if (hasTime && time < 0.0f)
						{
							throw ResourceException(animSetRes, "animation with the name '" + animName + "' has frame with a 'time' value less than zero.");
						}

						string xoffStr;
						bool hasXoff = framesNode->getOptionalAttribute("xoff", xoffStr);
						int xoff = hasXoff ? StringUtils::parseUInt(xoffStr) : 0;

						string yoffStr;
						bool hasYoff = framesNode->getOptionalAttribute("yoff", yoffStr);
						int yoff = hasYoff ? StringUtils::parseUInt(yoffStr) : 0;

						// Parse individual frames
						auto imagesetRes = static_cast<ImageSetResource*>(animSetRes->getDependentResource("Image").get());
						AnimationSetResource::FrameSet frameset;

						if (hasImageset)
						{
							auto const& imagesetDef = imagesetRes->getImagesetDefinition(imageset);

							// If count has been specified, then check it's not more than
							// the number of frames in the imageset.  If it hasn't been
							// specified, then set it to the number of frames.
							if (hasCount)
							{
								if (count > imagesetDef.size())
								{
									throw ResourceException(animSetRes, "animation with the name '" + animName + "' has 'count' value greater than the number of frames in the imageset it references.");
								}
							}
							else
							{
								count = (uint32_t)imagesetDef.size();
							}

							// Create all 'count' frames, then check the Frames nodes to see
							// if any need to be overridden.
							for (size_t i = 0; i < count; ++i)
							{
								auto const& imageDef = imagesetDef[i];
								frameset.push_back(createFrame(imageDef, xoff, yoff, time));
							}
						}
						else
						{
							// Create frames from definition.
							auto frameNode = framesNode->getOptionalChild("Frame");
							if (frameNode)
							{
								do
								{
									auto image = frameNode->getAttribute("image");
									auto const& imageDef = imagesetRes->getImageDefinition(image);
									frameset.push_back(createFrame(imageDef, xoff, yoff, time));
								} while (frameNode->next());
							}
						}

						checkFrameOverrides(animSetRes, animName, &frameset, framesNode, hasImageset);
						anim.frames = frameset;

						// Add to lookup
						addAnimation(animSetRes, animName, anim);
					} while (animationNode->next());
				}
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE