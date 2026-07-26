#pragma once

#include <fmod/core/fmod.hpp>
#include <fmod/core/fmod_errors.h>
#include <fmod/studio/fmod_studio.hpp>

#include "willpower/application/Platform.h"
#include "willpower/application/AudioOptions.h"
#include "willpower/application/resourcesystem/Resource.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		namespace resourcesystem
		{
			class AudioBankResource;
		}

		class WP_APPLICATION_API AudioSystem
		{
			FMOD::Studio::System* mSystem;

		public:

			explicit AudioSystem(AudioOptions const& options);

			~AudioSystem();

			void createAudioBank(resourcesystem::AudioBankResource* audioBank, resourcesystem::DataStreamPtr dataPtr);

			FMOD::Studio::EventInstance* startEvent(std::string const& eventName);

			void setEventVolume(FMOD::Studio::EventInstance* inst, float volume);

			void update();
		};

	} // application
} // WP_NAMESPACE

