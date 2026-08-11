#pragma once

#include "willpower/application/Platform.h"

#if WP_PLATFORM == WP_PLATFORM_WINDOWS

#include <windows.h>

namespace WP_NAMESPACE {
namespace application {

class WP_APPLICATION_API SchedulerTask {
  LARGE_INTEGER mFrequency, mStartTime;

  int mMicroseconds;

  bool mExecuting;

private:
  virtual void executeImpl(float frameTime) = 0;

protected:
  int getMicrosecondsSpent() const;

public:
  SchedulerTask();

  void setMicrosecondsAllocated(int microseconds);

  int getMicrosecondsAllocated() const;

  void execute(float frameTime);
};

}  // namespace application
}  // namespace WP_NAMESPACE

#else
#error "Willpower SchedulerTask is supported only on Windows."
#endif
