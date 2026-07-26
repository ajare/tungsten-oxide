#pragma once

#include <string>
#include <exception>

namespace WP_NAMESPACE
{
		
	class XmlException : public std::exception
	{
	public:

		XmlException(std::string const& msg, std::string const& source)
			: exception(source == "" ? msg.c_str() : std::string(source + ": " + msg).c_str())
		{
		}
	};

	class XmlPathException : public XmlException
	{
	public:

		XmlPathException(std::string const& path, std::string const& source)
			: XmlException("Path does not exist: " + path, source)
		{
		}
	};

} //  utils