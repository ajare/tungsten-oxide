#pragma once

#include <string>
#include <stdexcept>

#include "Platform.h"


class GameException : public std::runtime_error
{
public:

	explicit GameException(std::string const& msg)
		: std::runtime_error(msg)
	{
	}
};
