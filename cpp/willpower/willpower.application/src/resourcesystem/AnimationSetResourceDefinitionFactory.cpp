#include "willpower/common/StringUtils.h"
#include "willpower/common/DataNode.h"

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/AnimationSetResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;

AnimationSetResourceDefinitionFactory::AnimationSetResourceDefinitionFactory(string const& factoryType)
    : ResourceDefinitionFactory("AnimationSet", factoryType) {
}

void AnimationSetResourceDefinitionFactory::clear(AnimationSetResource* resource) {
  resource->mAnimations.clear();
}

AnimationSetResource::LoopStyle AnimationSetResourceDefinitionFactory::parseLoopStyle(AnimationSetResource* resource, string const& animation, DataNode* node) {
  string loopStyle;
  if (!node->getOptionalProperty("loopStyle", loopStyle)) {
    loopStyle = "forwards";
  }

  AnimationSetResource::LoopStyle ls;
  if (loopStyle == "forwards") {
    // Once finished, start from beginning again
    ls = AnimationSetResource::LoopStyle::Forwards;
  } else if (loopStyle == "once") {
    // Once finished, stay on last frame
    ls = AnimationSetResource::LoopStyle::Once;
  } else if (loopStyle == "pingpong") {
    // Once finished, start playing backwards to the first frame,
    // before starting again.
    ls = AnimationSetResource::LoopStyle::PingPong;
  } else {
    throw ResourceException(resource, "animation '" + animation + "' has unknown loop style '" + loopStyle + "'.");
  }

  return ls;
}

void AnimationSetResourceDefinitionFactory::parseFrame(AnimationSetResource const* resource, AnimationSetResource::Frame* frame, DataNode* node) {
  string timeStr;
  if (node->getOptionalProperty("time", timeStr)) {
    frame->time = StringUtils::parseFloat(timeStr);

    if (frame->time < 0) {
      throw ResourceException(resource, "animation with the name '" + resource->getName() + "' has frame with a 'time' value less than zero.");
    }
  }

  string xoffStr;
  if (node->getOptionalProperty("xoff", xoffStr)) {
    frame->renderOffsetX = StringUtils::parseInt(xoffStr);
  }

  string yoffStr;
  if (node->getOptionalProperty("yoff", yoffStr)) {
    frame->renderOffsetY = StringUtils::parseInt(yoffStr);
  }

  // Tags
  auto tagsNode = node->getOptionalChild("Tags");
  if (tagsNode) {
    auto tagNode = tagsNode->getOptionalChild("Tag");
    do {
      parseTag(frame, tagNode);
    } while (tagNode->next());
  }
}

void AnimationSetResourceDefinitionFactory::parseTag(AnimationSetResource::Frame* frame, DataNode* node) {
  auto key = node->getProperty("key");
  auto value = node->getProperty("value");

  frame->tags[key] = value;
}

void AnimationSetResourceDefinitionFactory::checkFrameOverrides(AnimationSetResource* resource, string const& animation, AnimationSetResource::FrameSet* frameset, DataNode* node, bool requireIndex) {
  auto& fs = *frameset;

  uint32_t defaultFrameIndex = 0;
  auto frameNode = node->getOptionalChild("Frame");
  if (frameNode) {
    auto count = fs.size();

    do {
      // Work out the frame index to use.
      string frameStr;
      uint32_t thisFrameIndex{~0u};
      if (requireIndex) {
        frameStr = frameNode->getProperty("frame");
        thisFrameIndex = StringUtils::parseUInt(frameStr);
      } else if (frameNode->getOptionalProperty("frame", frameStr)) {
        thisFrameIndex = StringUtils::parseUInt(frameStr);
      }

      auto fi = thisFrameIndex == ~0u ? defaultFrameIndex : thisFrameIndex;

      if (fi >= count) {
        throw ResourceException(resource, "animation with the name '" + animation + "' has a frame with an out-of-bounds index.");
      }

      parseFrame(resource, &fs[fi], frameNode);
      defaultFrameIndex++;
    } while (frameNode->next());
  }

  // Check time has been specified for all frames now, unless there is only one frame, in which case we set to 0.
  for (auto& frame : fs) {
    if (frame.time <= 0) {
      throw ResourceException(resource, "animation with the name '" + animation + "' has a frame with either no time, or time=0 (time must be > 0).");
    }
  }
}

AnimationSetResource::Frame AnimationSetResourceDefinitionFactory::createFrame(ImageSetResource::ImageDefinition const& imageDef, int offx, int offy, float time) {
  AnimationSetResource::Frame frame;

  frame.srcPosX = imageDef.x;
  frame.srcPosY = imageDef.y;
  frame.width = imageDef.width;
  frame.height = imageDef.height;
  frame.renderOffsetX = offx;
  frame.renderOffsetY = offy;

  for (int i = 0; i < 2; ++i) {
    frame.u[i] = imageDef.u[i];
    frame.v[i] = imageDef.v[i];
  }

  frame.time = time;

  return frame;
}

void AnimationSetResourceDefinitionFactory::addAnimation(AnimationSetResource* resource, string const& name, AnimationSetResource::Animation const& anim) {
  resource->mAnimations[name] = anim;
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE