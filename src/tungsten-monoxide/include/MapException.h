#pragma once

#include <string>
#include <exception>

#include "Platform.h"


class MapException : public std::exception
{
public:

	explicit MapException(std::string const& msg)
		: exception(msg.c_str())
	{
	}
};
