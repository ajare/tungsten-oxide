#pragma once

#include <string>
#include <fstream>

#include "willpower/common/Platform.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API Logger
	{
		enum MessageLevel
		{
			Debug,
			Warning,
			Error,
			Fatal,
			Info
		};

	private:

		std::ofstream mLog;

		std::string mFileName;

		int mIgnoreLevel;

	private:

		void message(std::string const& msg, MessageLevel level = MessageLevel::Info);

	public:

		Logger();

		~Logger();

		bool open(std::string const& filename);

		void debug(std::string const& msg);
		
		void info(std::string const& msg);

		void warn(std::string const& msg);

		void error(std::string const& msg);

		void fatal(std::string const& msg);
	};

} // WP_NAMESPACE
