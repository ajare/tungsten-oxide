#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateUnload.h"
#include "ModelInstance.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	StateUnload::StateUnload(bool useThreading)
		: ThreadableLoadState("Unload", Action::Unload, useThreading)
	{
	}

	ThreadableLoadState::LoadFunction StateUnload::getWorkFunction(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
	{
		WP_UNUSED(renderSystem);
		WP_UNUSED(renderResourceMgr);
		WP_UNUSED(args);

		return bind(&StateUnload::unloadResources, this, resourceMgr);
	}

	void StateUnload::unloadResources(application::resourcesystem::ResourceManager* resourceMgr)
	{
		// Unload resources
		auto resources = resourceMgr->getNamespaceResources("");

		mWillpowerResourcesToLoad.clear();
		mWillpowerResourcesToUnload.clear();

		copy_if(resources.begin(), resources.end(), back_inserter(mWillpowerResourcesToUnload), [resourceMgr](auto res)
		{
			return resourceMgr->isResourceLoaded(res);
		});

		if (usingThreading())
		{
			int resourceCount = (int)mWillpowerResourcesToUnload.size();

			if (resourceCount > 0)
			{
				for (auto res : mWillpowerResourcesToUnload)
				{
					addPendingResourceName(res->getName());
				}

				auto callback = bind(&StateUnload::unloadResourceCallback,
					this,
					placeholders::_1,
					placeholders::_2,
					placeholders::_3);

				for (auto resource : mWillpowerResourcesToUnload)
				{
					resourceMgr->releaseResource(resource, callback);
				}

				mWillpowerResourcesToUnload.clear();
			}
		}
	}


} // applib