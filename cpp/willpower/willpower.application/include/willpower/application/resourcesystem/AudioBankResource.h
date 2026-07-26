#pragma once

#include <string>

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/AnimationSetResourceDefinitionFactory.h"
#include "willpower/application/AudioSystem.h"


namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			class WP_APPLICATION_API AudioBankResource : public Resource
			{
				friend class AudioBankResourceDefinitionFactory;

				friend class AudioSystem;

			private:

				AudioSystem* mwAudioSystem;

				FMOD::Studio::Bank* mBank;

			private:

				void create(wp::application::resourcesystem::DataStreamPtr dataPtr, wp::application::resourcesystem::ResourceManager* resourceMgr) override;

				void destroy() override;

				bool load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

				bool unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

			public:

				AudioBankResource(std::string const& name,
					std::string const& namesp,
					std::string const& source,
					std::map<std::string, std::string> const& tags,
					wp::application::resourcesystem::ResourceLocation* location,
					AudioSystem* audioSystem);

				~AudioBankResource();
			};

			class AudioBankResourceFactory : public wp::application::resourcesystem::ResourceFactory
			{
				AudioSystem* mwAudioSystem;

			public:

				explicit AudioBankResourceFactory(AudioSystem* audioSystem)
					: wp::application::resourcesystem::ResourceFactory("AudioBank")
					, mwAudioSystem(audioSystem)
				{
				}

				wp::application::resourcesystem::Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, wp::application::resourcesystem::ResourceLocation* location) override
				{
					return new AudioBankResource(name, namesp, source, tags, location, mwAudioSystem);
				}
			};

		} // resourcesystem
	} // application
} // WP_NAMESPACE
