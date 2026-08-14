#pragma once

#include "willpower/common/DataNode.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/AnimationSetResource.h"
#include "willpower/application/resourcesystem/ImageSetResource.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
class WP_APPLICATION_API AnimationSetResourceDefinitionFactory : public ResourceDefinitionFactory {
protected:
  void clear(AnimationSetResource* resource);

  AnimationSetResource::LoopStyle parseLoopStyle(AnimationSetResource* resource, std::string const& animation, wp::DataNode* node);

  void parseFrame(AnimationSetResource const* resource, AnimationSetResource::Frame* frame, wp::DataNode* node);

  void parseTag(AnimationSetResource::Frame* frame, wp::DataNode* node);

  void checkFrameOverrides(AnimationSetResource* resource, std::string const& animation, AnimationSetResource::FrameSet* frameset, wp::DataNode* node, bool requireIndex);

  AnimationSetResource::Frame createFrame(ImageSetResource::ImageDefinition const& imageDef, int offx, int offy, float time);

  void addAnimation(AnimationSetResource* resource, std::string const& name, AnimationSetResource::Animation const& anim);

public:
  explicit AnimationSetResourceDefinitionFactory(std::string const& factoryType);
};

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
