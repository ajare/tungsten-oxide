#pragma once

#include <applib/EntityProperties.h>

#include "Platform.h"


enum EntityProperties
{
	// This must be strictly ascending from applib::EntityProperty::NumCoreProperties, and the highest entry cannot be 32 or greater.
	// If we do need more than 32 properties, then Entity::properties needs to be a uint64_t
	EP_Stats = applib::EntityProperty::NumCoreProperties
};
