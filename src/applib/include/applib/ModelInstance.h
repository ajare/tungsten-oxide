#pragma once

#include "Platform.h"
#include "Model.h"

namespace applib
{

	class APPLIB_API ModelInstance
	{
		static Model* msModel;

	public:

		static void set(Model* model);

		static Model* get();

		static std::shared_ptr<EntityHandler> entityHandler();
	};

} // applib