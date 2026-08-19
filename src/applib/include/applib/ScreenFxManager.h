#pragma once

#include <vector>
#include <functional>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/UniformCollection.h>
#include <mpp/Texture.h>

#include <willpower/common/Vector2.h>

#include "Platform.h"

namespace applib
{

	class APPLIB_API ScreenFxManager
	{
		struct FullscreenFlash
		{
			mpp::Colour colour;
			float attack, hold, release, timer;
		};

	private:

		mpp::Texture* mwFullscreenTexture;

		std::shared_ptr<mpp::UniformCollection> mFullscreenUniforms;

		std::vector<FullscreenFlash> mFlashes;

	public:

		explicit ScreenFxManager(mpp::ResourceManager* renderResourceMgr);

		~ScreenFxManager();

		void flash(mpp::Colour const& colour, float attack, float hold, float release);

		void update(float frameTime);

		void preRender(wp::Vector2 const& viewPos);

		void postRender(mpp::RenderSystem* renderSystem);
	};

} // applib