#pragma once

#include "Platform.h"

namespace applib
{

	// This must be strictly ascending from zero, and the highest entry cannot be 32 or greater.
	// If we do need more than 32 properties, then Entity::properties needs to be a uint64_t
	enum EntityProperty
	{
		EP_Physical,
		EP_Visual,
		EP_BeamEmitter,
		NumCoreProperties
	};

} // applib