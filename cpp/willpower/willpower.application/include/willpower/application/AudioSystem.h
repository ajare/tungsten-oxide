#pragma once

#include "willpower/application/Platform.h"

#if WP_PLATFORM == WP_PLATFORM_WINDOWS && defined(WP_APPLICATION_USE_FMOD)
#include <fmod/core/fmod.hpp>
#include <fmod/core/fmod_errors.h>
#include <fmod/studio/fmod_studio.hpp>
#endif
#include "willpower/application/AudioOptions.h"
#include "willpower/application/resourcesystem/Resource.h"

namespace WP_NAMESPACE {
namespace application {

namespace resourcesystem {
class AudioBankResource;
}

class WP_APPLICATION_API AudioSystem {
#if WP_PLATFORM == WP_PLATFORM_WINDOWS && defined(WP_APPLICATION_USE_FMOD)
  FMOD::Studio::System* mSystem;
#endif

public:
  explicit AudioSystem(AudioOptions const& options);

  ~AudioSystem();

  void createAudioBank(resourcesystem::AudioBankResource* audioBank, resourcesystem::DataStreamPtr dataPtr);

#if WP_PLATFORM == WP_PLATFORM_WINDOWS && defined(WP_APPLICATION_USE_FMOD)
  FMOD::Studio::EventInstance* startEvent(std::string const& eventName);

  void setEventVolume(FMOD::Studio::EventInstance* inst, float volume);
#endif

  void update();
};

}  // namespace application
}  // namespace WP_NAMESPACE
