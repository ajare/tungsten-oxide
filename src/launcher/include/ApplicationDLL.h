#pragma once

#include "Platform.h"

#if APP_PLATFORM == APP_PLATFORM_WINDOWS

#include <Windows.h>

#include <string>
#include <vector>

#include "willpower/application/StateFactory.h"
#include "willpower/application/resourcesystem/ResourceManager.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"

#include "willpower/common/Logger.h"

#include "StateManager.h"

class ApplicationDLL {
  typedef char const* (*DllGetNameFunction)();

  typedef wp::application::StateFactory* (*DllGetNextStateFactoryFunction)();

  typedef void (*DllOnEntryFunction)(wp::Logger*, wp::application::resourcesystem::ResourceManager*);

  typedef void (*DllOnExitFunction)();

  typedef int (*DllSetArgumentFunction)(char const*, char const*);

private:
#if APP_PLATFORM == APP_PLATFORM_WINDOWS
  HINSTANCE mGetProcIDDLL;
#endif

  std::string mFilepath;

  std::string mName;

  // Required DLL functions
  DllGetNameFunction mGetNameFunction;

  DllGetNextStateFactoryFunction mGetNextStateFactoryFunction;

  DllSetArgumentFunction mSetArgumentFunction;

  static std::string msGetNameFunction;

  static std::string msCreateApplicationFunctionName, msDestroyApplicationFunctionName;

  static std::string msGetNextStateFactoryFunctionName;

  static std::string msGetNextResourceFactoryFunctionName;

  static std::string msSetArgumentFunctionName;

  // Optional DLL functions
  DllOnEntryFunction mOnEntryFunction;

  DllOnExitFunction mOnExitFunction;

  static std::string msOnEntryFunctionName, msOnExitFunctionName;

private:
  void registerRequiredFunctions();

  void registerOptionalFunctions();

public:
  ApplicationDLL();

  ~ApplicationDLL();

  std::string const& getFilepath() const;

  void load(std::string const& file, std::map<std::string, std::string> const& arguments, wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr);

  void unload();

  std::string getApplicationName() const;

  void registerStateFactories(StateManager* stateMgr);
};

#else
#error "Launcher application DLL loading is supported only on Windows."
#endif