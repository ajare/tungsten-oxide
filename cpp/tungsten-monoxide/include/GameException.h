#pragma once

#include <string>
#include <exception>

#include "Platform.h"


class GameException : public std::exception
{
public:

	explicit GameException(std::string const& msg)
		: exception(msg.c_str())
	{
	}
};
