#pragma once

#include <string>
#include <exception>

#include "Platform.h"

namespace applib
{

	class Exception : public std::exception
	{
		std::string message_;

	public:
		explicit Exception(std::string const& message)
			: message_(message)
		{
		}

		const char* what() const noexcept override { return message_.c_str(); }
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
