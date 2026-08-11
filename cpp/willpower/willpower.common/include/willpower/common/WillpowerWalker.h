#pragma once

#include <string>
#include <vector>
#include <cassert>

#include "willpower/common/Platform.h"

#if WP_PLATFORM == WP_PLATFORM_WINDOWS

#include "willpower/common/StackWalker.h"
#include "willpower/common/Logger.h"

#ifdef WP_USE_ASSERT_TRACE
#ifdef _DEBUG
#define ASSERT_TRACE(expr)                                        \
  if (!(expr)) {                                                  \
    StackWalkerInstance::getInstance()->logStackTraceFormatted(); \
    assert(expr);                                                 \
  }
#else
#define ASSERT_TRACE(expr) (void)0
#endif
#else
#define ASSERT_TRACE(expr) assert(expr)
#endif

namespace WP_NAMESPACE {

class WP_COMMON_API WillpowerWalker : public StackWalker {
  Logger* mLogger;

  bool mNewTrace;

protected:
  void OnOutput(LPCSTR szText);

public:
  WillpowerWalker(std::string const& logfile);

  ~WillpowerWalker();

  void logStackTraceFormatted();
};

class WP_COMMON_API StackWalkerInstance {
  static WillpowerWalker* mInstance;

protected:
  StackWalkerInstance();

public:
  static WillpowerWalker* getInstance();

  static bool hasInstance();

  static void deleteInstance();
};

}  // namespace WP_NAMESPACE

#else
#error "Willpower stack-trace support is available only on Windows."
#endif
