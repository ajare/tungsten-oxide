#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateLoad.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	StateLoad::StateLoad(bool useThreading)
		: ThreadableLoadState("Load", Action::Load, useThreading)
	{
	}

	ThreadableLoadState::LoadFunction StateLoad::getWorkFunction(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
	{
		WP_UNUSED(renderSystem);
		WP_UNUSED(renderResourceMgr);
		WP_UNUSED(args);

		return bind(&StateLoad::loadResources, this, resourceMgr);
	}

	void StateLoad::scanResourceLocationCallback(string const& locationName, wp::application::resourcesystem::ResourceLocationState state)
	{
		string text;
		switch (state)
		{
		case application::resourcesystem::ResourceLocationState::Unscanned:
			text = "Scanning " + locationName;
			break;
		default:
			return;
		}

		addText(text);
	}

	void StateLoad::loadResources(application::resourcesystem::ResourceManager* resourceMgr)
	{
		auto scanCallbackFn = bind(&StateLoad::scanResourceLocationCallback,
			this,
			placeholders::_1,
			placeholders::_2);

		resourceMgr->scanLocations(scanCallbackFn);

		// Load resources
		auto resources = resourceMgr->getNamespaceResources("");

		mWillpowerResourcesToLoad.clear();
		mWillpowerResourcesToUnload.clear();
		copy_if(resources.begin(), resources.end(), back_inserter(mWillpowerResourcesToLoad), [resourceMgr](auto res)
		{
			return !resourceMgr->isResourceLoaded(res);
		});

		if (usingThreading())
		{
			int resourceCount = (int)mWillpowerResourcesToLoad.size();

			if (resourceCount > 0)
			{
				for (auto res : mWillpowerResourcesToLoad)
				{
					addPendingResourceName(res->getName());
				}

				auto loadCallbackFn = bind(&StateLoad::loadResourceCallback,
					this,
					placeholders::_1,
					placeholders::_2,
					placeholders::_3);

				for (auto resource : mWillpowerResourcesToLoad)
				{
					resourceMgr->createResource(resource, loadCallbackFn);
					resourceMgr->loadResource(resource, loadCallbackFn);
				}

				mWillpowerResourcesToLoad.clear();
			}
		}

		processPostWork({ });
	}


} // applib