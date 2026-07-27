#pragma once

#include <string>
#include <exception>

#include "Platform.h"

namespace applib
{

	class Exception : public std::exception
	{
	public:

		explicit Exception(std::string const& message)
			: std::exception(message.c_str())
		{
		}
	};

	class NotImplementedException : public Exception
	{
	public:

		NotImplementedException()
			: Exception("Not implemented yet.")
		{
		}

		explicit NotImplementedException(std::string const& function)
			: Exception(function + " is not implemented yet.")
		{
		}

		NotImplementedException(std::string const& function, std::string const& msg)
			: Exception(function + ": " + msg + " is not implemented yet.")
		{
		}
	};

} // applib
