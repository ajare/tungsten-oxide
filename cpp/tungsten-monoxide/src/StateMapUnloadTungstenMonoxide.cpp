#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateMapUnloadTungstenMonoxide.h"

using namespace std;
using namespace wp;


StateMapUnloadTungstenMonoxide::StateMapUnloadTungstenMonoxide(bool useThreading)
	: applib::StateMapUnload(useThreading)
{
}

vector<applib::ThreadableLoadState::ThreadableWorkFunction> StateMapUnloadTungstenMonoxide::getPreWork(applib::StateTransitionData* transitionData)
{
	VAR_UNUSED(transitionData);

	return {};
}