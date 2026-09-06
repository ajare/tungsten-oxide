#include "Platform.h"

#include <format>
#include <stdexcept>

#include "ApplicationDLL.h"

using namespace std;

#if APP_PLATFORM != APP_PLATFORM_WINDOWS
#define GetProcAddress(handle, name) dlsym((handle), (name))
#define FreeLibrary(handle) dlclose((handle))
#endif

string ApplicationDLL::msGetNameFunction = "dllGetName";
string ApplicationDLL::msGetNextStateFactoryFunctionName = "dllGetNextStateFactory";
string ApplicationDLL::msGetNextResourceFactoryFunctionName = "dllGetNextResourceFactory";
string ApplicationDLL::msOnEntryFunctionName = "dllOnEntry";
string ApplicationDLL::msOnExitFunctionName = "dllOnExit";
string ApplicationDLL::msSetArgumentFunctionName = "dllSetArgument";

ApplicationDLL::ApplicationDLL()
    : mGetProcIDDLL(0), mGetNameFunction(0), mGetNextStateFactoryFunction(0), mOnEntryFunction(0), mOnExitFunction(0) {
}

ApplicationDLL::~ApplicationDLL() {
  unload();
}

string const& ApplicationDLL::getFilepath() const {
  return mFilepath;
}

void ApplicationDLL::registerRequiredFunctions() {
  mGetNameFunction = (DllGetNameFunction)GetProcAddress(mGetProcIDDLL, msGetNameFunction.c_str());

  if (!mGetNameFunction) {
    string errMsg = "Could not find DLL function '" + msGetNameFunction + "' in '" + mFilepath + "'.";
    throw runtime_error(errMsg);
  }

  mGetNextStateFactoryFunction = (DllGetNextStateFactoryFunction)GetProcAddress(mGetProcIDDLL, msGetNextStateFactoryFunctionName.c_str());

  if (!mGetNextStateFactoryFunction) {
    string errMsg = "Could not find DLL function '" + msGetNextStateFactoryFunctionName + "' in '" + mFilepath + "'.";
    throw runtime_error(errMsg);
  }

  mSetArgumentFunction = (DllSetArgumentFunction)GetProcAddress(mGetProcIDDLL, msSetArgumentFunctionName.c_str());

  if (!mSetArgumentFunction) {
    string errMsg = "Could not find DLL function '" + msSetArgumentFunctionName + "' in '" + mFilepath + "'.";
    throw runtime_error(errMsg);
  }
}

void ApplicationDLL::registerOptionalFunctions() {
  mOnEntryFunction = (DllOnEntryFunction)GetProcAddress(mGetProcIDDLL, msOnEntryFunctionName.c_str());
  mOnExitFunction = (DllOnExitFunction)GetProcAddress(mGetProcIDDLL, msOnExitFunctionName.c_str());
}

void ApplicationDLL::load(string const& filepath, map<string, string> const& arguments, wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr) {
  mFilepath = filepath;

#if APP_PLATFORM == APP_PLATFORM_WINDOWS

  // Load DLL
  mGetProcIDDLL = LoadLibrary(wstring(mFilepath.begin(), mFilepath.end()).c_str());

  if (!mGetProcIDDLL) {
    auto err = GetLastError();
    string errMsg = std::format("Could not load '{}'.  Error code: {}", mFilepath, err);
    throw runtime_error(errMsg);
  }
#else
  mGetProcIDDLL = dlopen(mFilepath.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!mGetProcIDDLL) {
    throw runtime_error(format("Could not load '{}': {}", mFilepath, dlerror()));
  }
#endif

  registerRequiredFunctions();
  registerOptionalFunctions();

  // Pass in arguments
  for (auto const& argument : arguments) {
    if (mSetArgumentFunction(argument.first.c_str(), argument.second.c_str()) != 0) {
      string errMsg = format("Application could not parse config argument: {}={}", argument.first, argument.second);
      throw runtime_error(errMsg);
    }
  }

  // Call entry function
  if (mOnEntryFunction) {
    mOnEntryFunction(logger, resourceMgr);
  }
}

void ApplicationDLL::unload() {
  // Call exit function
  if (mOnExitFunction) {
    mOnExitFunction();
  }

  if (mGetProcIDDLL != 0) {
    FreeLibrary(mGetProcIDDLL);
    mGetProcIDDLL = 0;
  }
}

string ApplicationDLL::getApplicationName() const {
  return string(mGetNameFunction());
}

void ApplicationDLL::registerStateFactories(StateManager* stateMgr) {
  auto stateFactory = mGetNextStateFactoryFunction();
  while (stateFactory) {
    stateMgr->registerStateFactory(stateFactory);
    stateFactory = mGetNextStateFactoryFunction();
  }
}
