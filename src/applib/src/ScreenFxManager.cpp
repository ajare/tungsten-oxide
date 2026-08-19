#include "ScreenFxManager.h"

namespace applib
{
	using namespace std;
	using namespace wp;

	ScreenFxManager::ScreenFxManager(mpp::ResourceManager* renderResourceMgr)
	{
		mwFullscreenTexture = static_cast<mpp::Texture*>(renderResourceMgr->getResource("__mpp_tex_none__").get());

		mFullscreenUniforms = make_shared<mpp::UniformCollection>();
		mFullscreenUniforms->setUniform("DIFFUSE", glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
	}

	ScreenFxManager::~ScreenFxManager()
	{

	}

	void ScreenFxManager::flash(mpp::Colour const& colour, float attack, float hold, float release)
	{
		assert(attack >= 0.0f && "ScreenFxManager::flash() attack time must be greater than zero.");
		assert(hold >= 0.0f && "ScreenFxManager::flash() hold time must be greater than zero.");
		assert(release >= 0.0f && "ScreenFxManager::flash() release time must be greater than zero.");

		FullscreenFlash flash
		{
			colour,
			attack,
			hold,
			release,
			0
		};

		mFlashes.push_back(flash);
	}

	void ScreenFxManager::update(float frameTime)
	{
		mpp::Colour mix(0, 0, 0, 0);

		uint32_t count = 0;
		for (size_t i = 0; i < mFlashes.size(); ++i)
		{
			auto& flash = mFlashes[i];

			float d1 = flash.attack;
			float d2 = d1 + flash.hold;
			float d3 = d2 + flash.release;

			flash.timer += frameTime;

			if (flash.timer < d3)
			{
				// Are we in attack, hold or release phase?
				float delta;
				if (flash.timer <= d1)
				{
					delta = flash.attack != 0 ? (flash.timer / flash.attack) : 1.0f;
				}
				else if (flash.timer <= d2)
				{
					delta = 1.0f;
				}
				else if (flash.timer <= d3)
				{
					delta = 1.0f - (flash.release != 0 ? ((flash.timer - d2) / flash.release) : 1.0f);
				}

				// Interpolate and build up colour
				auto lerped = flash.colour * delta;
				
				mix.red += lerped.red;
				mix.green += lerped.green;
				mix.blue += lerped.blue;
				mix.alpha += lerped.alpha;

				if (i != count)
				{
					mFlashes[i] = mFlashes[i];
				}

				count++;
			}
		}

		// Remove finished flashes
		while (mFlashes.size() > count)
		{
			mFlashes.pop_back();
		}

		// Scale
		if (count > 0)
		{
			mix /= (float)count;
		}

		mFullscreenUniforms->updateUniform("DIFFUSE", glm::vec4(mix.red, mix.green, mix.blue, mix.alpha));
	}

	void ScreenFxManager::preRender(wp::Vector2 const& viewPos)
	{

	}

	void ScreenFxManager::postRender(mpp::RenderSystem* renderSystem)
	{
		renderSystem->renderToScreen();
		renderSystem->renderFullscreenQuad(mwFullscreenTexture, mpp::BlendMode::SrcAlpha, mpp::BlendMode::OneMinusSrcAlpha, mFullscreenUniforms);

	}

} // applib