#pragma once

#include <string>
#include <stdexcept>

#include "Platform.h"


class MapException : public std::runtime_error
{
public:

	explicit MapException(std::string const& msg)
		: std::runtime_error(msg)
	{
	}
};
