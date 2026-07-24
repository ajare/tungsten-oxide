#include <regex>

#include <willpower/common/StringUtils.h>

#include "willpower/common/WillpowerWalker.h"

namespace WP_NAMESPACE
{

	using namespace std;
	using namespace utils;

	// Singleton instantiation
	WillpowerWalker* StackWalkerInstance::mInstance = nullptr;

	WillpowerWalker::WillpowerWalker(string const& logfile)
		//: StackWalker(StackWalkOptions::RetrieveLine | StackWalkOptions::RetrieveSymbol)
		: StackWalker()
		, mNewTrace(true)
	{
		mLogger = new Logger();
		mLogger->open(logfile);
	}

	WillpowerWalker::~WillpowerWalker()
	{
		delete mLogger;
	}

	void WillpowerWalker::OnOutput(LPCSTR szText)
	{
		string msg(szText);

		// Only print what we care about, which is the actual callstack.
		// Format is: <filepath><whitespace>(<line-number>)<colon><whitespace><function>
			
		// Dirty hack time
		string thisFilepath = __FILE__;
		size_t stempos = thisFilepath.find("willpower\\willpower\\willpower.common\\src\\willpowerwalker.cpp");
		string filestem = thisFilepath.substr(0, stempos);

		if (StringUtils::startsWith(msg, filestem) &&
			!StringUtils::endsWith(msg, "ShowCallstack") &&
			!StringUtils::endsWith(msg, "logStackTraceFormatted"))
		{
			mLogger->info(msg);
		}

		mNewTrace = false;
	}

	void WillpowerWalker::logStackTraceFormatted()
	{
		mNewTrace = true;
		ShowCallstack();
	}

	StackWalkerInstance::StackWalkerInstance()
	{
	}

	WillpowerWalker* StackWalkerInstance::getInstance()
	{
		if (!mInstance)
		{
			mInstance = new WillpowerWalker("DebugStackTracer.html");
		}

		return mInstance;
	}

	bool StackWalkerInstance::hasInstance()
	{
		return mInstance != nullptr;
	}

	void StackWalkerInstance::deleteInstance()
	{
		delete mInstance;
		mInstance = nullptr;
	}
	
} // WP_NAMESPACE
