#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateMapUnloadBooleanWorld.h"
#include "WorldRenderer.h"

using namespace std;
using namespace wp;


StateMapUnloadBooleanWorld::StateMapUnloadBooleanWorld(bool useThreading)
	: applib::StateMapUnload(useThreading)
{
}

vector<applib::ThreadableLoadState::ThreadableWorkFunction> StateMapUnloadBooleanWorld::getPreWork(applib::StateTransitionData* transitionData)
{
	VAR_UNUSED(transitionData);

	auto destroyWorldRendererFn = [this](bool useThreading)
	{
		VAR_UNUSED(useThreading);

		addText("Destroying world renderer");

		auto worldRenderer = static_cast<WorldRenderer*>(this->mTransitionData.userData);

		delete worldRenderer;
		this->mTransitionData.userData = nullptr;
	};

	return { destroyWorldRendererFn };
}