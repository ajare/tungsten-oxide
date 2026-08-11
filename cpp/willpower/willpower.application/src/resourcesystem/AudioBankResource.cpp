#ifdef WP_APPLICATION_USE_FMOD
#include <fmod/core/fmod.hpp>
#include <fmod/core/fmod_errors.h>
#include <fmod/studio/fmod_studio.hpp>
#endif

#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/application/resourcesystem/AudioBankResource.h"


namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			using namespace std;
			using namespace wp;

			AudioBankResource::AudioBankResource(string const& name,
				string const& namesp,
				string const& source,
				map<string, string> const& tags,
				application::resourcesystem::ResourceLocation* location,
				AudioSystem* audioSystem)
				: application::resourcesystem::Resource(name, namesp, "AudioBank", source, tags, location)
				, mwAudioSystem(audioSystem)
#ifdef WP_APPLICATION_USE_FMOD
				, mBank(nullptr)
#endif
			{
			}

			AudioBankResource::~AudioBankResource()
			{
			}

			void AudioBankResource::create(application::resourcesystem::DataStreamPtr dataPtr, application::resourcesystem::ResourceManager* resourceMgr)
			{
				parseData(dataPtr);
				parseDefinition(resourceMgr);

				if (mwAudioSystem)
				{
					mwAudioSystem->createAudioBank(this, dataPtr);
				}
			}

			void AudioBankResource::destroy()
			{
#ifdef WP_APPLICATION_USE_FMOD
				if (mwAudioSystem)
				{
					auto res = mBank->unload();
					mBank = nullptr;

					if (res != FMOD_OK)
					{
						throw application::resourcesystem::ResourceException(this, (FMOD_ErrorString(res)));
					}
				}
#endif
			}

			bool AudioBankResource::load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
			{
				WP_UNUSED(renderSystem);
				WP_UNUSED(resourceMgr);

#ifdef WP_APPLICATION_USE_FMOD
				if (mwAudioSystem)
				{
					auto res = mBank->loadSampleData();

					if (res != FMOD_OK)
					{
						throw application::resourcesystem::ResourceException(this, (FMOD_ErrorString(res)));
					}
				}
#endif

				return true;
			}

			bool AudioBankResource::unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
			{
				WP_UNUSED(renderSystem);
				WP_UNUSED(resourceMgr);

#ifdef WP_APPLICATION_USE_FMOD
				if (mwAudioSystem)
				{
					auto res = mBank->unloadSampleData();

					if (res != FMOD_OK)
					{
						throw application::resourcesystem::ResourceException(this, (FMOD_ErrorString(res)));
					}
				}
#endif

				return true;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE