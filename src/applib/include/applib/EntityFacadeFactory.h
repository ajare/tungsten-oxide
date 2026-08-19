#pragma once

#include "Platform.h"
#include "EntityFacade.h"

namespace applib
{

	class EntityFacadeFactory
	{
	public:

		virtual ~EntityFacadeFactory() = default;

		virtual EntityFacade* create(size_t initialSize) = 0;
	};

} // applib
