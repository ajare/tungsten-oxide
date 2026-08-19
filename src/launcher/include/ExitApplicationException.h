#pragma once

#include <exception>
#include <string>

class ExitApplicationException : public std::exception
{
	int mExitCode;

	std::string mMessage;

public:

	ExitApplicationException(int exitCode, std::string message)
		: std::exception(message.c_str())
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