#pragma once

#include "willpower/application/Platform.h"

#if WP_PLATFORM == WP_PLATFORM_WINDOWS

#include <windows.h>
#include <vector>
#include "willpower/application/SchedulerTask.h"

namespace WP_NAMESPACE {
namespace application {

class WP_APPLICATION_API Scheduler {
  struct CurrentTask {
    SchedulerTask* task;
    int cumulativeMilliseconds;
  };

private:
  LARGE_INTEGER mFrequency;

  int mMicroseconds, mTotalMicrosecondsAllocated;

  std::vector<CurrentTask> mTasks;

  bool mExecuting;

public:
  explicit Scheduler(int microseconds);

  void setMicrosecondsAllocated(int microseconds);

  int getMicrosecondsAllocated() const;

  void addTask(SchedulerTask* task, int microseconds);

  void execute(float frameTime);
};

}  // namespace application
}  // namespace WP_NAMESPACE

#else
#error "Willpower Scheduler is supported only on Windows."
#endif
