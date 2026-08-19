#include "ModelInstance.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	Model* ModelInstance::msModel = nullptr;

	void ModelInstance::set(Model* model)
	{
		msModel = model;
	}

	Model* ModelInstance::get()
	{
		return msModel;
	}

	shared_ptr<EntityHandler> ModelInstance::entityHandler()
	{
		return get()->entityHandler;
	}

} // applib