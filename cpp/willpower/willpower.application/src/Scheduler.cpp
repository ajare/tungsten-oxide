#include "willpower/application/Platform.h"

#if WP_PLATFORM == WP_PLATFORM_WINDOWS

#include "willpower/application/Scheduler.h"

#include "willpower/common/Exceptions.h"

using namespace std;

namespace WP_NAMESPACE {
namespace application {

Scheduler::Scheduler(int microseconds)
    : mMicroseconds(microseconds), mTotalMicrosecondsAllocated(0), mExecuting(false) {
  if (::QueryPerformanceFrequency(&mFrequency) == FALSE) {
    throw Exception("Could not get timer frequency.");
  }
}

void Scheduler::setMicrosecondsAllocated(int microseconds) {
  if (mExecuting) {
    throw Exception("Cannot set scheduler timeslice when executing.");
  }

  mMicroseconds = microseconds;
}

int Scheduler::getMicrosecondsAllocated() const {
  return mMicroseconds;
}

void Scheduler::addTask(SchedulerTask* task, int microseconds) {
  task->setMicrosecondsAllocated(microseconds);
  mTotalMicrosecondsAllocated += microseconds;

  CurrentTask ct;

  ct.task = task;
  ct.cumulativeMilliseconds = 0;

  if (!mTasks.empty()) {
    ct.cumulativeMilliseconds = mTasks.back().cumulativeMilliseconds + mTasks.back().task->getMicrosecondsAllocated();
  }

  mTasks.push_back(ct);
}

void Scheduler::execute(float frameTime) {
  mExecuting = true;

  // Start timer
  LARGE_INTEGER startTime, taskStartTime, taskEndTime;
  if (::QueryPerformanceCounter(&startTime) == FALSE) {
    throw Exception("Could not get timer time.");
  }

  int microsecondsLeft = mMicroseconds;
  float scaleRemaining = 1.0f;
  for (auto task : mTasks) {
    // Get task start time
    if (::QueryPerformanceCounter(&taskStartTime) == FALSE) {
      throw Exception("Could not get timer time.");
    }

    int taskMicroseconds = task.task->getMicrosecondsAllocated();
    if (taskMicroseconds > 0) {
      task.task->setMicrosecondsAllocated((int)(taskMicroseconds * scaleRemaining));
    }

    task.task->execute(frameTime);

    // Get task end time
    if (::QueryPerformanceCounter(&taskEndTime) == FALSE) {
      throw Exception("Could not get timer time.");
    }

    double interval = static_cast<double>(taskEndTime.QuadPart - taskStartTime.QuadPart) / mFrequency.QuadPart;
    int microsecondsPassed = (int)(interval * 1000000);
    microsecondsLeft -= microsecondsPassed;

    // If the total time allocated to the remaining tasks is greater than the time
    // left, then scale each down.
    int microsecondsRequired = mTotalMicrosecondsAllocated - task.cumulativeMilliseconds;
    if (microsecondsRequired > microsecondsLeft) {
      scaleRemaining *= (float)microsecondsLeft / (float)microsecondsRequired;
    }
  }

  mExecuting = false;
}

}  // namespace application
}  // namespace WP_NAMESPACE

#else
#error "Willpower Scheduler implementation is supported only on Windows."
#endif
