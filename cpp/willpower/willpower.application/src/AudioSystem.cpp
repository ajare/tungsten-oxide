#include <exception>

#include "willpower/application/AudioSystem.h"
#include "willpower/application/resourcesystem/AudioBankResource.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"


namespace WP_NAMESPACE
{
	namespace application
	{

		using namespace std;

#ifdef WP_APPLICATION_USE_FMOD
		AudioSystem::AudioSystem(AudioOptions const& options)
			: mSystem(nullptr)
		{
			FMOD_RESULT res;

			// Create audio system
			res = FMOD::Studio::System::create(&mSystem);
			if (res != FMOD_OK)
			{
				throw exception(FMOD_ErrorString(res));
			}

			// Create core system
			FMOD::System* coreSystem{ nullptr };

			res = mSystem->getCoreSystem(&coreSystem);
			if (res != FMOD_OK)
			{
				throw exception(FMOD_ErrorString(res));
			}

			// Set up system
			res = coreSystem->setSoftwareFormat(0, FMOD_SPEAKERMODE_5POINT1, 0);
			if (res != FMOD_OK)
			{
				throw exception(FMOD_ErrorString(res));
			}

			FMOD_STUDIO_INITFLAGS studioFlags = 0 | options.synchronous
				? FMOD_STUDIO_INIT_SYNCHRONOUS_UPDATE
				: FMOD_STUDIO_INIT_NORMAL;

			res = mSystem->initialize(options.numChannels, studioFlags, FMOD_INIT_NORMAL, nullptr);
			if (res != FMOD_OK)
			{
				throw exception(FMOD_ErrorString(res));
			}
		}

		AudioSystem::~AudioSystem()
		{
			FMOD_RESULT res;

			res = mSystem->release();
			if (res != FMOD_OK)
			{
				// Log an error?
			}
		}

		void AudioSystem::createAudioBank(resourcesystem::AudioBankResource* audioBank, resourcesystem::DataStreamPtr dataPtr)
		{
			auto data = (char const*)dataPtr->getData();

			auto res = mSystem->loadBankMemory(data, dataPtr->getSize(), FMOD_STUDIO_LOAD_MEMORY, FMOD_STUDIO_LOAD_BANK_NORMAL, &audioBank->mBank);

			if (res != FMOD_OK)
			{
				throw resourcesystem::ResourceException(audioBank, FMOD_ErrorString(res));
			}
		}

		FMOD::Studio::EventInstance* AudioSystem::startEvent(string const& eventName)
		{
			FMOD::Studio::EventDescription* desc{ nullptr };

			// Get event
			string eventEventName = format("event:/{}", eventName);
			auto res = mSystem->getEvent(eventEventName.c_str(), &desc);

			if (res != FMOD_OK)
			{
				throw exception(FMOD_ErrorString(res));
			}

			// Create instance
			FMOD::Studio::EventInstance* inst{ nullptr };
			res = desc->createInstance(&inst);

			if (res != FMOD_OK)
			{
				throw exception(FMOD_ErrorString(res));
			}

			// Start instance
			res = inst->start();

			if (res != FMOD_OK)
			{
				throw exception(FMOD_ErrorString(res));
			}

			return inst;
		}

		void AudioSystem::setEventVolume(FMOD::Studio::EventInstance* inst, float volume)
		{
			auto res = inst->setVolume(volume);

			if (res != FMOD_OK)
			{
				throw exception(FMOD_ErrorString(res));
			}
		}

		void AudioSystem::update()
		{
			auto res = mSystem->update();
			if (res != FMOD_OK)
			{
				throw exception(FMOD_ErrorString(res));
			}
		}
#else
		AudioSystem::AudioSystem(AudioOptions const& options)
		{
			WP_UNUSED(options);
		}

		AudioSystem::~AudioSystem() = default;

		void AudioSystem::createAudioBank(resourcesystem::AudioBankResource* audioBank, resourcesystem::DataStreamPtr dataPtr)
		{
			WP_UNUSED(audioBank);
			WP_UNUSED(dataPtr);
		}

		void AudioSystem::update()
		{
		}
#endif

	} // application
} // WP_NAMESPACE