#include <iomanip>
#include <ctime>

#include "willpower/common/Logger.h"

namespace WP_NAMESPACE
{

	using namespace std;

	Logger::Logger () :
		mIgnoreLevel(MessageLevel::Debug)
	{
	}

	Logger::~Logger ()
	{
		message("Logger: shutting down.");

		if(mLog.is_open())
		{
			// HTML footer	
			mLog << "</body>" << std::endl;
			mLog << "</html>" << std::endl;
	
			mLog.close();
		}
	}

	bool Logger::open(string const& filename)
	{
		mFileName = filename;

		mLog.open(filename.c_str());
	
		if (!mLog.is_open())
		{
			// No point adding an error message!
			return false;
		}

		// HTML header
		mLog << "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"" << std::endl;
		mLog << "\"http://www.w3.org/TR/html4/loose.dtd\">" << std::endl;
		mLog << "<html>" << std::endl;
		mLog << "<head>" << std::endl;
		mLog << "<title>Log file</title>" << std::endl;
		mLog << "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=iso-8859-1\">" << std::endl;
		mLog << "</head>" << std::endl;
		mLog << "<body>" << std::endl;

		std::string buildText;
#ifdef _DEBUG
		buildText = "Debug";
#else
		buildText = "Release";
#endif

		message("Logger: initialised successfully.");
		return true;
	}

	void Logger::message(string const& msg, MessageLevel level)
	{
		if (!mLog.is_open() || level <= mIgnoreLevel)
		{
			return;
		}
	
		struct tm pTime;
		time_t ctTime; time(&ctTime);
		localtime_s(&pTime, &ctTime);

		switch (level)
		{
		case MessageLevel::Info:
			mLog << "<font color=\"#000000\">";
			break;

		case MessageLevel::Debug:
			mLog << "<font color=\"#6666DD\">";
			break;

		case MessageLevel::Warning:
			mLog << "<font color=\"#FF7700\">";
			break;

		case MessageLevel::Error:
		case MessageLevel::Fatal:
			mLog << "<font color=\"#FF0000\">";
			break;

		default:
			break;
		}

		mLog << setw(2) << setfill('0') << pTime.tm_hour
			<< ":" << setw(2) << setfill('0') << pTime.tm_min
			<< ":" << setw(2) << setfill('0') << pTime.tm_sec
			<< ": " << msg;

		mLog << "</font><br>" << std::endl;
			
		// Flush stcmdream to ensure it is written (in case of a crash
		// we need log to be up to date).
		mLog.flush();
	}

	void Logger::debug(string const& msg)
	{
		message(msg, MessageLevel::Debug);
	}

	void Logger::info(string const& msg)
	{
		message(msg, MessageLevel::Info);
	}

	void Logger::warn(string const& msg)
	{
		message(msg, MessageLevel::Warning);
	}

	void Logger::error(string const& msg)
	{
		message(msg, MessageLevel::Error);
	}

	void Logger::fatal(string const& msg)
	{
		message(msg, MessageLevel::Fatal);
	}

} // WP_NAMESPACE
