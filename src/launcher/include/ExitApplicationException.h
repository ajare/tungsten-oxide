#pragma once

#include <stdexcept>
#include <string>

class ExitApplicationException : public std::runtime_error
{
	int mExitCode;

	std::string mMessage;

public:

	ExitApplicationException(int exitCode, std::string message)
		: std::runtime_error(message)
		, mExitCode(exitCode)
		, mMessage(message)
	{
	}

	int getExitCode() const
	{
		return mExitCode;
	}

	std::string const& getMessage() const
	{
		return mMessage;
	}
};