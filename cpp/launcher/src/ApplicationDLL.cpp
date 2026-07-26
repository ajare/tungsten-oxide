#include "ApplicationDLL.h"

using namespace std;

string ApplicationDLL::msGetNameFunction = "dllGetName";
string ApplicationDLL::msGetNextStateFactoryFunctionName = "dllGetNextStateFactory";
string ApplicationDLL::msGetNextResourceFactoryFunctionName = "dllGetNextResourceFactory";
string ApplicationDLL::msOnEntryFunctionName = "dllOnEntry";
string ApplicationDLL::msOnExitFunctionName = "dllOnExit";
string ApplicationDLL::msSetArgumentFunctionName = "dllSetArgument";

ApplicationDLL::ApplicationDLL()
	: mGetProcIDDLL(0)
	, mGetNameFunction(0)
	, mGetNextStateFactoryFunction(0)
	, mOnEntryFunction(0)
	, mOnExitFunction(0)
{
}

ApplicationDLL::~ApplicationDLL()
{
	unload();
}

string const& ApplicationDLL::getFilepath() const
{
	return mFilepath;
}

void ApplicationDLL::registerRequiredFunctions()
{
	mGetNameFunction = (DllGetNameFunction)GetProcAddress(mGetProcIDDLL, msGetNameFunction.c_str());

	if (!mGetNameFunction)
	{
		string errMsg = "Could not find DLL function '" + msGetNameFunction + "' in '" + mFilepath + "'.";
		throw exception(errMsg.c_str());
	}

	mGetNextStateFactoryFunction = (DllGetNextStateFactoryFunction)GetProcAddress(mGetProcIDDLL, msGetNextStateFactoryFunctionName.c_str());

	if (!mGetNextStateFactoryFunction)
	{
		string errMsg = "Could not find DLL function '" + msGetNextStateFactoryFunctionName + "' in '" + mFilepath + "'.";
		throw exception(errMsg.c_str());
	}

	mSetArgumentFunction = (DllSetArgumentFunction)GetProcAddress(mGetProcIDDLL, msSetArgumentFunctionName.c_str());

	if (!mSetArgumentFunction)
	{
		string errMsg = "Could not find DLL function '" + msSetArgumentFunctionName + "' in '" + mFilepath + "'.";
		throw exception(errMsg.c_str());
	}
}

void ApplicationDLL::registerOptionalFunctions()
{
	mOnEntryFunction = (DllOnEntryFunction)GetProcAddress(mGetProcIDDLL, msOnEntryFunctionName.c_str());
	mOnExitFunction = (DllOnExitFunction)GetProcAddress(mGetProcIDDLL, msOnExitFunctionName.c_str());
}

void ApplicationDLL::load(string const& filepath, map<string, string> const& arguments, wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr)
{
	mFilepath = filepath;

#if APP_PLATFORM == APP_PLATFORM_WINDOWS

	// Load DLL
	mGetProcIDDLL = LoadLibrary(wstring(mFilepath.begin(), mFilepath.end()).c_str());

	if (!mGetProcIDDLL)
	{
		auto err = GetLastError();
		string errMsg = STR_FORMAT("Could not load '{}'.  Error code: {}", mFilepath, err);
		throw exception(errMsg.c_str());
	}

	registerRequiredFunctions();
	registerOptionalFunctions();

	// Pass in arguments
	for (auto const& argument : arguments)
	{
		if (mSetArgumentFunction(argument.first.c_str(), argument.second.c_str()) != 0)
		{
			string errMsg = format("Application could not parse config argument: {}={}", argument.first, argument.second);
			throw exception(errMsg.c_str());
		}
	}

	// Call entry function
	if (mOnEntryFunction)
	{
		mOnEntryFunction(logger, resourceMgr);
	}

#else
	throw exception("DLL loading for non-Windows platforms is not yet implemented.");
#endif
}

void ApplicationDLL::unload()
{
#if APP_PLATFORM == APP_PLATFORM_WINDOWS

	// Call exit function
	if (mOnExitFunction)
	{
		mOnExitFunction();
	}

	if (mGetProcIDDLL != 0)
	{
		FreeLibrary(mGetProcIDDLL);
		mGetProcIDDLL = 0;
	}
#else
	throw exception("DLL loading for non-Windows platforms is not yet implemented.");
#endif
}

string ApplicationDLL::getApplicationName() const
{
	return string(mGetNameFunction());
}

void ApplicationDLL::registerStateFactories(StateManager* stateMgr)
{
	auto stateFactory = mGetNextStateFactoryFunction();
	while (stateFactory)
	{
		stateMgr->registerStateFactory(stateFactory);
		stateFactory = mGetNextStateFactoryFunction();
	}
}
